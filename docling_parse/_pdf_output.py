"""Complete binary stream writes and atomic replacement of PDF output paths."""

from __future__ import annotations

import errno
import os
import secrets
import stat
import sys
import tempfile
from contextlib import suppress
from io import FileIO
from typing import BinaryIO

from docling_parse.pdf_errors import PdfEditErrorCategory, PdfWriteError

_UNSUPPORTED = {errno.ENOTSUP, errno.EOPNOTSUPP}


def write_all(stream: BinaryIO, data: bytes) -> None:
    remaining: bytes | memoryview = data
    while remaining:
        count = stream.write(remaining)
        if type(count) is not int or not 0 < count <= len(remaining):
            raise PdfWriteError(
                PdfEditErrorCategory.WRITE,
                "binary stream did not report a valid, positive write count",
            )
        remaining = memoryview(remaining)[count:]


def _target_stat(path: str) -> os.stat_result | None:
    try:
        info = os.lstat(path)
    except FileNotFoundError:
        return None
    if not stat.S_ISREG(info.st_mode):
        raise OSError(f"PDF output target is not a regular file: {path}")
    if info.st_nlink != 1:
        raise OSError(f"PDF output target has multiple hard links: {path}")
    return info


def _darwin_acl(fd: int, source: str | None) -> None:
    import ctypes

    libc = ctypes.CDLL(None, use_errno=True)
    libc.acl_get_link_np.argtypes = (ctypes.c_char_p, ctypes.c_int)
    libc.acl_get_link_np.restype = ctypes.c_void_p
    libc.acl_init.argtypes = (ctypes.c_int,)
    libc.acl_init.restype = ctypes.c_void_p
    libc.acl_set_fd_np.argtypes = (ctypes.c_int, ctypes.c_void_p, ctypes.c_int)
    libc.acl_free.argtypes = (ctypes.c_void_p,)
    extended = 0x100  # ACL_TYPE_EXTENDED
    acl = None
    if source is not None:
        acl = libc.acl_get_link_np(os.fsencode(source), extended)
        if not acl:
            error = ctypes.get_errno()
            if error in _UNSUPPORTED:
                return
            if error not in {errno.ENOENT, errno.ENODATA}:
                raise OSError(error, os.strerror(error), source)
    if not acl:
        acl = libc.acl_init(0)
        if not acl:
            error = ctypes.get_errno()
            raise OSError(error, os.strerror(error))
    try:
        if libc.acl_set_fd_np(fd, acl, extended) != 0:
            error = ctypes.get_errno()
            if error not in _UNSUPPORTED:
                raise OSError(error, os.strerror(error))
    finally:
        libc.acl_free(acl)


def _linux_acl(fd: int, source: str | None) -> None:
    name = "system.posix_acl_access"
    if source is not None:
        try:
            acl = os.getxattr(source, name, follow_symlinks=False)  # type: ignore[attr-defined]
        except OSError as exc:
            if exc.errno not in _UNSUPPORTED | {errno.ENODATA}:
                raise
        else:
            os.setxattr(fd, name, acl)  # type: ignore[attr-defined]
            return
    try:
        os.removexattr(fd, name)  # type: ignore[attr-defined]
    except OSError as exc:
        if exc.errno not in _UNSUPPORTED | {errno.ENODATA}:
            raise


def _windows_temporary(parent: str) -> tuple[int, str]:
    import msvcrt

    import ntsecuritycon  # type: ignore[import-untyped]
    import pywintypes  # type: ignore[import-untyped]
    import win32api  # type: ignore[import-untyped]
    import win32con  # type: ignore[import-untyped]
    import win32file  # type: ignore[import-untyped]
    import win32security as security  # type: ignore[import-untyped]

    try:
        token = security.OpenProcessToken(
            win32api.GetCurrentProcess(), win32con.TOKEN_QUERY
        )
        try:
            user, _ = security.GetTokenInformation(token, security.TokenUser)
        finally:
            token.Close()
        acl = security.ACL()
        acl.AddAccessAllowedAce(
            security.ACL_REVISION, ntsecuritycon.FILE_ALL_ACCESS, user
        )
        descriptor = security.SECURITY_DESCRIPTOR()
        descriptor.SetSecurityDescriptorDacl(True, acl, False)
        descriptor.SetSecurityDescriptorControl(
            security.SE_DACL_PROTECTED, security.SE_DACL_PROTECTED
        )
        attributes = pywintypes.SECURITY_ATTRIBUTES()
        attributes.SECURITY_DESCRIPTOR = descriptor
        for _ in range(10):
            path = os.path.join(parent, ".docling-" + secrets.token_hex(12))
            try:
                handle = win32file.CreateFile(
                    path,
                    win32con.GENERIC_ALL,
                    0,
                    attributes,
                    win32con.CREATE_NEW,
                    win32con.FILE_ATTRIBUTE_NORMAL,
                    None,
                )
            except pywintypes.error as exc:
                if exc.winerror in (80, 183):  # File already exists.
                    continue
                raise
            raw_handle = handle.Detach()
            try:
                flags = os.O_WRONLY | os.O_BINARY | os.O_NOINHERIT  # type: ignore[attr-defined]
                return msvcrt.open_osfhandle(raw_handle, flags), path  # type: ignore[attr-defined]
            except OSError:
                win32api.CloseHandle(raw_handle)
                os.unlink(path)
                raise
        raise FileExistsError("could not allocate a unique PDF temporary file")
    except pywintypes.error as exc:
        raise OSError(str(exc)) from exc


def _windows_acl(fd: int, source: str) -> None:
    import msvcrt

    import pywintypes  # type: ignore[import-untyped]
    import win32security as security  # type: ignore[import-untyped]

    owner = group = None
    flags = security.DACL_SECURITY_INFORMATION
    handle = msvcrt.get_osfhandle(fd)  # type: ignore[attr-defined]
    try:
        ownership = (
            security.OWNER_SECURITY_INFORMATION | security.GROUP_SECURITY_INFORMATION
        )
        existing = security.GetNamedSecurityInfo(
            source, security.SE_FILE_OBJECT, flags | ownership
        )
        temporary = security.GetSecurityInfo(handle, security.SE_FILE_OBJECT, ownership)
        existing_owner = existing.GetSecurityDescriptorOwner()
        existing_group = existing.GetSecurityDescriptorGroup()
        if existing_owner != temporary.GetSecurityDescriptorOwner():
            owner = existing_owner
            flags |= security.OWNER_SECURITY_INFORMATION
        if existing_group != temporary.GetSecurityDescriptorGroup():
            group = existing_group
            flags |= security.GROUP_SECURITY_INFORMATION
        acl = existing.GetSecurityDescriptorDacl()
        control, _ = existing.GetSecurityDescriptorControl()
        flags |= (
            security.PROTECTED_DACL_SECURITY_INFORMATION
            if control & security.SE_DACL_PROTECTED
            else security.UNPROTECTED_DACL_SECURITY_INFORMATION
        )
        security.SetSecurityInfo(
            handle, security.SE_FILE_OBJECT, flags, owner, group, acl, None
        )
    except pywintypes.error as exc:
        raise OSError(str(exc)) from exc


def _create_temporary(parent: str) -> tuple[int, str]:
    if os.name == "nt":
        return _windows_temporary(parent)
    return tempfile.mkstemp(prefix=".docling-", dir=parent)


def _set_acl(fd: int, source: str | None = None) -> None:
    if os.name == "nt":
        if source is not None:
            _windows_acl(fd, source)
    elif sys.platform == "darwin":
        _darwin_acl(fd, source)
    elif sys.platform.startswith("linux"):
        _linux_acl(fd, source)


def _cleanup_temporary(path: str) -> None:
    with suppress(FileNotFoundError):
        try:
            os.unlink(path)
        except PermissionError:
            if os.name != "nt":
                raise
            os.chmod(path, stat.S_IWRITE)
            os.unlink(path)


def _sync_directory(parent: str) -> None:
    if not hasattr(os, "O_DIRECTORY"):
        return
    fd = os.open(parent, os.O_RDONLY | os.O_DIRECTORY)
    try:
        os.fsync(fd)
    except OSError as exc:
        if exc.errno not in _UNSUPPORTED | {errno.EINVAL}:
            raise
    finally:
        os.close(fd)


def write_path(target: str | os.PathLike[str], data: bytes) -> FileIO:
    path = os.fspath(target)
    try:
        resolved = os.path.realpath(path)
        parent = os.path.dirname(resolved)
        original = _target_stat(resolved)
        fd, temporary = _create_temporary(parent)
        try:
            with FileIO(fd, "wb") as stream:
                stream.name = path
                _set_acl(fd)
                write_all(stream, data)
                if original is not None:
                    if hasattr(os, "fchown"):
                        temporary_info = os.fstat(fd)
                        if (temporary_info.st_uid, temporary_info.st_gid) != (
                            original.st_uid,
                            original.st_gid,
                        ):
                            os.fchown(fd, original.st_uid, original.st_gid)
                    mode = stat.S_IMODE(original.st_mode)
                    if hasattr(os, "fchmod"):
                        os.fchmod(fd, mode)
                    else:
                        os.chmod(temporary, mode)
                    _set_acl(fd, resolved)
                os.fsync(fd)

            current = _target_stat(resolved)
            if (original is None) != (current is None) or (
                original is not None
                and current is not None
                and not os.path.samestat(original, current)
            ):
                raise OSError(f"PDF output target changed during the write: {resolved}")
            os.replace(temporary, resolved)
            _sync_directory(parent)
        finally:
            _cleanup_temporary(temporary)
        return stream
    except OSError as exc:
        raise PdfWriteError(PdfEditErrorCategory.WRITE, str(exc)) from exc
