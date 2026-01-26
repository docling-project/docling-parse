# CMap Resources

This open source project provides the latest CMap resources for Adobe’s public character collections.

CMap (*Character Map*) resources are used to unidirectionally map character codes, such as a Unicode encoding form, to CIDs (*Characters IDs*, meaning glyphs) of a CIDFont resource. CMap resources, like CIDFont resources, include a /CIDSystemInfo dictionary that specifies /Registry and /Ordering strings that must match for compatibility between the two resources. In other words, a CMap resource can be used with a CIDFont resource only if their /Registry and /Ordering strings match.

A character collection is sometimes referred to as an **ROS**, which is an abbreviation for the three elements of the /CIDSystemInfo dictionary, specifically the **/Registry** string, the **/Ordering** string, and the **/Supplement** value (an integer). When fully specified, the three elements are separate using single hyphens, such as **Adobe-Japan1-7** (broken down into /Registry = *Adobe*, /Ordering = *Japan1*, and /Supplement = *7*).

Although originally designed for use with CIDFont resources, CMap resources are also used for other purposes, such as by the [AFDKO](https://github.com/adobe-type-tools/afdko) *makeotf* tool for building 'cmap' tables for OpenType/CFF fonts when the source font is a CIDFont resource.

## Contents

This project includes the following CMap resources, grouped by character collection:

*Adobe-Identity-0*: Adobe-Identity-0 (Special Purpose)  
*Adobe-CNS1-7*: [Adobe-CNS1-7](https://github.com/adobe-type-tools/Adobe-CNS1/) (Traditional Chinese, including Hong Kong SCS-2016)  
*Adobe-GB1-5*: [Adobe-GB1-5](https://github.com/adobe-type-tools/Adobe-GB1/) (Simplified Chinese)  
*Adobe-Japan1-7*: [Adobe-Japan1-7](https://github.com/adobe-type-tools/Adobe-Japan1/) (Japanese)  
*Adobe-Korea1-2*: [Adobe-Korea1-2](https://github.com/adobe-type-tools/Adobe-KR/raw/master/5093.Adobe-Korea1-2.pdf) (Korean)  
*Adobe-KR-9*: [Adobe-KR-9](https://github.com/adobe-type-tools/Adobe-KR/) (Korean)  
*deprecated/Adobe-Japan2-0*: [Adobe-Japan2-0](https://github.com/adobe-type-tools/Adobe-Japan1/raw/master/5097.Adobe-Japan2-0.pdf) (Japanese, for JIS X 0212-1990 only; Deprecated and replaced by Adobe-Japan1-6)

The *VERSIONS.txt* file specifies the current version of each CMap resource.

Each collection of CMap resources includes a *cid2code.txt* file that provides the mappings for each CID in tabular form. The Adobe-Japan1-7 CMap resources additionally include mapping files for the three JIS standards, JIS X 0208, JIS X 0212, and JIS X 0213, for JIS90- (*-jp90*) and JIS2004-savvy (*-jp04*) implementations. (The deprecated Adobe-Japan2-0 CMap resources include JIS X 0212 mapping files.)

## CMap Resources Versus 'cmap' Tables

CMap resources should not be confused with '[cmap](https://docs.microsoft.com/en-us/typography/opentype/spec/cmap)' tables of sfnt-based fonts, such as OpenType and TrueType. While they are functionally similar, in that both unidirectionally map character codes, a 'cmap' table maps them to GIDs (*Glyph IDs*). For some fonts, such as OpenType fonts that are based on one of these character collections and include every glyph, CIDs can equal GIDs, but it is not guaranteed, thus the importance of the distinction.

## Compiling/Decompiling CMap Resources

The [Perl Scripts](https://github.com/adobe-type-tools/perl-scripts) open source project includes a *cmap-tool.pl* script that can be used to compile and decompile CMap resources.

## CMap Resource Descriptions

The sections that follow provide detailed information about the CMap resources that are included in this project, on a per character collection basis, and the following conventions and notes apply:

* References to code points and code space ranges are expressed in hexadecimal notation, and use a leading “0x.”  
* Every two hexadecimal digits correspond to one byte.  
* The encodings described in this document range from one- to four-byte.  
* Whenever a horizontal CMap resource is modified to support a newer Supplement, its corresponding vertical CMap resource is also modified to reflect the same Supplement, regardless of whether mappings were added or not.  
* Although still listed, the UCS-2 CMap files are now considered obsolete, and are no longer being maintained. They have been replaced&mdash;for all character collections&mdash;with a suite of UTF-8, UTF-16, and UTF-32 CMap resources that include the same number of mappings.

For more information about the character sets and encodings that are referenced in the sections below, please reference [*CJKV Information Processing*, Second Edition](http://shop.oreilly.com/product/9780596514471.do) (O’Reilly Media, 2009).

### The Adobe-CNS1-7 Character Collection

The table below lists the supported encodings:

**Encoding** | **Byte Ranges**
--- | ---
ISO-2022 | 0x2121–0x7E7E
EUC-TW | 0x00–0x80, 0xA1A1–0xFEFE, 0x8EA1A1A1–0x8EA1FEFE, 0x8EA2A1A1–0x8EA2FEFE & 0x8EA3A1A1–0x8EA3FEFE
Big Five | 0x00–0x80, 0xFD–0xFF (only in B5pc) & 0x8140–0xFEFE (0xA140–0xFCFE in B5pc; 0x8140–0xFEFE in HKscs)
UCS-2 | 0x0000–0xD7FF & 0xE000–0xFFFF
UTF-8 | 0x00–0x7F, 0xC080–0xDFBF, 0xE08080–0xEFBFBF & 0xF0808080–0xF7BFBFBF
UTF-16 | 0x0000–0xD7FF, 0xE000–0xFFFF & 0xD800DC00–0xDBFFDFFF
UTF-32 | 0x00000000–0x0010FFFF

The table below provides descriptions of the Adobe-CNS1-7 CMap resources:

**CMap Resource** | **Description**
--- | ---
Adobe-CNS1-0 | Identity CMap
Adobe-CNS1-1 | Identity CMap
Adobe-CNS1-2 | Identity CMap
Adobe-CNS1-3 | Identity CMap
Adobe-CNS1-4 | Identity CMap
Adobe-CNS1-5 | Identity CMap
Adobe-CNS1-6 | Identity CMap
Adobe-CNS1-7 | Identity CMap
B5-H | Big Five character set, Big Five encoding, halfwidth ASCII character set in one-byte range
B5-V | Vertical version of B5-H
B5pc-H | Apple Macintosh Traditional Chinese character set, Big Five encoding, proportional-width ASCII character set in one-byte range
B5pc-V | Vertical version of B5pc-H
CNS-EUC-H | CNS 11643 character set, Planes 1 and 2, EUC-TW encoding, half-width ASCII character set in one-byte range
CNS-EUC-V | Vertical version of CNS-EUC-H
CNS1-H | CNS 11643 character set, Plane 1, ISO-2022 encoding
CNS1-V | Vertical version of CNS1-H
CNS2-H | CNS 11643 character set, Plane 2, ISO-2022 encoding
CNS2-V | Vertical version of CNS2-H
ETen-B5-H | Big Five character set with ETen extensions, Big Five encoding, half-width ASCII character set in one-byte range
ETen-B5-V | Vertical version of ETen-B5-H
ETenms-B5-H | Big Five character set with ETen extensions, Big Five encoding, proportional ASCII character set in one-byte range
ETenms-B5-V | Vertical version of ETenms-B5-H
ETHK-B5-H | HKSCS plus ETen extensions, Big Five encoding
ETHK-B5-V | Vertical version of ETHK-B5-H
HKdla-B5-H | DynaComware’s larger Hong Kong–specific character set containing 784 hanzi, Big Five encoding (0xFA41–0xFEFE)
HKdla-B5-V | Vertical version of HKdla-B5-H
HKdlb-B5-H | DynaComware’s smaller Hong Kong–specific character set containing 665 hanzi, Big Five encoding (0x8E40–0x9265)
HKdlb-B5-V | Vertical version of HKdlb-B5-H
HKgccs-B5-H | Hong Kong GCCS, plus 145 additional hanzi, Big Five encoding (0x8A40–0x8B5A & 0x8E40–0xA0E6 and 0xFA40–0xFEFE)
HKgccs-B5-V | Vertical version of HKgccs-B5-H
HKm314-B5-H | Monotype’s smaller Hong Kong–specific character set containing 314 hanzi, being a complete subset of the larger set, Big Five encoding (0xC740–0xC8FE)
HKm314-B5-V | Vertical version of HKm314-B5-H
HKm471-B5-H | Monotype’s larger Hong Kong–specific character set containing 471 hanzi, Big Five encoding (0xFA40–0xFCFE)
HKm471-B5-V | Vertical version of HKm471-B5-H
HKscs-B5-H | HKSCS, Big Five encoding
HKscs-B5-V | Vertical version of HKscs-B5-H
UniCNS-UCS2-H | ISO/IEC 10646 (Unicode), UCS-2 encoding&mdash;OBSOLETE (use UniCNS-UTF16-H)
UniCNS-UCS2-V | Vertical version of UniCNS-UCS2-H&mdash;OBSOLETE (use UniCNS-UTF16-V)
UniCNS-UTF8-H | ISO/IEC 10646 (Unicode), UTF-8 encoding
UniCNS-UTF8-V | Vertical version of UniCNS-UTF8-H
UniCNS-UTF16-H | ISO/IEC 10646 (Unicode), UTF-16 encoding
UniCNS-UTF16-V | Vertical version of UniCNS-UTF16-H
UniCNS-UTF32-H | ISO/IEC 10646 (Unicode), UTF-32 encoding
UniCNS-UTF32-V | Vertical version of UniCNS-UTF32-H

### The Adobe-GB1-5 Character Collection

The table below lists the supported encodings:

**Encoding** | **Byte Ranges**
--- | ---
ISO-2022 | 0x2121–0x7E7E
EUC-CN | 0x00–0x80, 0xFD–0xFF (only in GBpc and GBTpc) & 0xA1A1–0xFEFE (0xA1A1–0xFCFE in GBpc and GBTpc)
GBK | 0x00–0x80 & 0x8140–0xFEFE
GB 18030 | 0x00–0x80, 0x8140–0xFEFE & 0x81308130–0xFE39FE39
UCS-2 | 0x0000–0xD7FF & 0xE000–0xFFFF
UTF-8 | 0x00–0x7F, 0xC080–0xDFBF, 0xE08080–0xEFBFBF & 0xF0808080–0xF7BFBFBF
UTF-16 | 0x0000–0xD7FF, 0xE000–0xFFFF & 0xD800DC00–0xDBFFDFFF
UTF-32 | 0x00000000–0x0010FFFF

The table below provides descriptions of the Adobe-GB1-5 CMap resources:

**CMap Resource** | **Description**
--- | ---
Adobe-GB1-0 | Identity CMap
Adobe-GB1-1 | Identity CMap
Adobe-GB1-2 | Identity CMap
Adobe-GB1-3 | Identity CMap
Adobe-GB1-4 | Identity CMap
Adobe-GB1-5 | Identity CMap
GB-H | GB/T 2312 character set, ISO-2022 encoding
GB-V | Vertical version of GB-H
GB-EUC-H | GB/T 2312 character set, EUC-CN encoding, half-width GB/T 1988 character set (nearly identical with ASCII) in one-byte range
GB-EUC-V | Vertical version of GB-EUC-H
GBpc-EUC-H | Apple Macintosh Simplified Chinese character set, EUC-CN encoding, proportional-width ASCII character set in one-byte range
GBpc-EUC-V | Vertical version of GBpc-EUC-H
GBK-EUC-H | GBK character set, GBK encoding
GBK-EUC-V | Vertical version of GBK-EUC-H
GBK2K-H | GB 18030 character set, GB 18030 encoding
GBK2K-V | Vertical version of GBK2K-H
GBT-H | GB/T 12345 character set, ISO-2022 encoding
GBT-V | Vertical version of GBT-H
GBT-EUC-H | GB/T 12345 character set, EUC-CN encoding, half-width GB/T 1988 character set (nearly identical with ASCII) in one-byte range
GBT-EUC-V | Vertical version of GBT-EUC-H
GBTpc-EUC-H | GB/T 12345 character set for Apple Macintosh, EUC-CN encoding, proportional-width ASCII character set in one-byte range
GBTpc-EUC-V | Vertical version of GBTpc-EUC-H
UniGB-UCS2-H | ISO/IEC 10646 (Unicode), UCS-2 encoding&mdash;OBSOLETE (use UniGB-UTF16-H)
UniGB-UCS2-V | Vertical version of UniGB-UCS2-H&mdash;OBSOLETE (use UniGB-UTF16-V)
UniGB-UTF8-H | ISO/IEC 10646 (Unicode), UTF-8 encoding
UniGB-UTF8-V | Vertical version of UniGB-UTF8-H
UniGB-UTF16-H | ISO/IEC 10646 (Unicode), UTF-16 encoding
UniGB-UTF16-V | Vertical version of UniGB-UTF16-H
UniGB-UTF32-H | ISO/IEC 10646 (Unicode), UTF-32 encoding
UniGB-UTF32-V | Vertical version of UniGB-UTF32-H

### The Adobe-Japan1-7 Character Collection

The table below lists the supported encodings:

**Encoding** | **Byte Ranges**
--- | ---
ISO-2022-JP | 0x2121–0x7E7E
EUC-JP | 0x00–0x80, 0x8EA0–0x8EDF & 0xA1A1–0xFEFE
Shift-JIS | 0x00–0x80, 0xA0–0xDF, 0xFD–0xFF (only in 83pv and 90pv), 0x8140–0x9FFC & 0xE040–0xFCFC
Other | 0x00–0xFF (Hankaku, Hiragana, Katakana, Roman & WP-Symbol)
UCS-2 | 0x0000–0xD7FF & 0xE000–0xFFFF
UTF-8 | 0x00–0x7F, 0xC080–0xDFBF, 0xE08080–0xEFBFBF & 0xF0808080–0xF7BFBFBF
UTF-16 | 0x0000–0xD7FF, 0xE000–0xFFFF & 0xD800DC00–0xDBFFDFFF
UTF-32 | 0x00000000–0x0010FFFF

The table below provides descriptions of the Adobe-Japan1-7 CMap resources:

**CMap Resource** | **Description**
--- | ---
Adobe-Japan1-0 | Identity CMap
Adobe-Japan1-1 | Identity CMap
Adobe-Japan1-2 | Identity CMap
Adobe-Japan1-3 | Identity CMap
Adobe-Japan1-4 | Identity CMap
Adobe-Japan1-5 | Identity CMap
Adobe-Japan1-6 | Identity CMap
Adobe-Japan1-7 | Identity CMap
H | JIS X 0208 character set, ISO-2022-JP encoding
V | Vertical version of H
RKSJ-H | JIS X 0208 character set, Shift-JIS encoding, half-width JIS-Roman character set (nearly identical with ASCII) in one-byte range
RKSJ-V | Vertical version of RKSJ-H
EUC-H | JIS X 0208 character set, EUC-JP encoding, half-width JIS-Roman character set (nearly identical with ASCII) in one-byte range (code set 0), half-width katakana character set in two-byte range (code set 2)
EUC-V | Vertical version of EUC-H
78-H | JIS C 6226-1978 character set, ISO-2022-JP encoding
78-V | Vertical version of 78-H
78-RKSJ-H | JIS C 6226-1978 character set, Shift-JIS encoding, half-width JIS-Roman character set (nearly identical with ASCII) in one-byte range
78-RKSJ-V | Vertical version of 78-RKSJ-H
78-EUC-H | JIS C 6226-1978 character set, EUC-JP encoding, half-width JIS-Roman character set (nearly identical with ASCII) in one-byte range (code set 0), half-width katakana character set in two-byte range (code set 2)
78-EUC-V | Vertical version of 78-EUC-H
78ms-RKSJ-H | Identical to 90ms-RKSJ-H, except that JIS C 6226-1978 forms are used, Shift-JIS encoding, half-width JIS-Roman character set (nearly identical with ASCII) in one-byte range
78ms-RKSJ-V | Vertical version of 78ms-RKSJ-H
83pv-RKSJ-H | Apple Macintosh KanjiTalk Version 6 character set, Shift-JIS encoding, proportional-width ASCII character set in one-byte range
90ms-RKSJ-H | Microsoft Windows 3.1J and Windows 95J character set, Shift-JIS encoding; half-width JIS-Roman character set (nearly identical with ASCII) in one-byte range
90ms-RKSJ-V | Vertical version of 90ms-RKSJ-H
90msp-RKSJ-H | Identical to 90ms-RKSJ-H, but using a proportional JIS-Roman character set (nearly identical with ASCII) in one-byte range, Shift-JIS encoding
90msp-RKSJ-V | Vertical version of 90msp-RKSJ-H
90pv-RKSJ-H | Apple Macintosh KanjiTalk Version 7 character set, Shift-JIS encoding, proportional-width ASCII character set in one-byte range
90pv-RKSJ-V | Vertical version of 90pv-RKSJ-H
Add-H | Fujitsu FMR character set, ISO-2022-JP encoding
Add-V | Vertical version of Add-H
Add-RKSJ-H | Fujitsu FMR character set, Shift-JIS encoding, half-width JIS-Roman character set (nearly identical with ASCII) in one-byte range
Add-RKSJ-V | Vertical version of Add-RKSJ-H
Ext-H | NEC character set, ISO-2022-JP encoding
Ext-V | Vertical version of Ext-H
Ext-RKSJ-H | NEC character set, Shift-JIS encoding, half-width JIS-Roman character set (nearly identical with ASCII) in one-byte range
Ext-RKSJ-V | Vertical version of Ext-RKSJ-H
NWP-H | NEC Bungō character set, ISO-2022-JP encoding
NWP-V | Vertical version of NWP-H
Hankaku | Half-width Latin, hiragana, and katakana in one-byte range
Hiragana | Half-width hiragana in one-byte range
Katakana | Half-width katakana in one-byte range
Roman | Half-width Latin in one-byte range
WP-Symbol | Miscellaneous symbols in one-byte range
UniJIS-UCS2-H | ISO/IEC 10646 (Unicode), UCS-2 encoding&mdash;OBSOLETE (use UniJIS-UTF16-H)
UniJIS-UCS2-V | Vertical version of UniJIS-UCS2-H&mdash;OBSOLETE (use UniJIS-UTF16-V)
UniJISPro-UCS2-V | Vertical version of UniJIS-UCS2-H with references to vertical forms added in Adobe-Japan1-4&mdash;OBSOLETE (use UniJIS-UTF16-V)
UniJIS-UCS2-HW-H | Same as UniJIS-UCS2-H, but with half-width Latin glyphs in the ASCII/JIS-Roman range (0x20–0x7E and 0xA5)&mdash;OBSOLETE (use UniJIS-UTF16-H)
UniJIS-UCS2-HW-V | Vertical version of UniJIS-UCS2-HW-H&mdash;OBSOLETE (use UniJIS-UTF16-V)
UniJISPro-UCS2-HW-V | Vertical version of UniJIS-UCS2-HW-H with references to vertical forms added in Adobe-Japan1-4&mdash;OBSOLETE (use UniJIS-UTF16-V)
UniJIS-UTF8-H | ISO/IEC 10646 (Unicode), UTF-8 encoding
UniJIS-UTF8-V | Vertical version of UniJIS-UTF8-H
UniJISPro-UTF8-V | Vertical version of UniJIS-UTF8-H with references to vertical forms added in Adobe-Japan1-4&mdash;OBSOLETE (use UniJIS-UTF8-V)
UniJIS-UTF16-H | ISO/IEC 10646 (Unicode), UTF-16 encoding
UniJIS-UTF16-V | Vertical version of UniJIS-UTF16-H
UniJIS-UTF32-H | ISO/IEC 10646 (Unicode), UTF-32 encoding
UniJIS-UTF32-V | Vertical version of UniJIS-UTF32-H
UniJIS2004-UTF8-H | ISO/IEC 10646 (Unicode), UTF-8 encoding, JIS2004 glyphs are default
UniJIS2004-UTF8-V | Vertical version of UniJIS2004-UTF8-H
UniJIS2004-UTF16-H | ISO/IEC 10646 (Unicode), UTF-16 encoding, JIS2004 glyphs are default
UniJIS2004-UTF16-V | Vertical version of UniJIS2004-UTF16-H
UniJIS2004-UTF32-H | ISO/IEC 10646 (Unicode), UTF-32 encoding, JIS2004 glyphs are default
UniJIS2004-UTF32-V | Vertical version of UniJIS2004-UTF32-H
UniJISX0213-UTF32-H | ISO/IEC 10646 (Unicode), UTF-32 encoding, macOS compatible
UniJISX0213-UTF32-V | Vertical version of UniJISX0213-UTF32-H
UniJISX02132004-UTF32-H | ISO/IEC 10646 (Unicode), UTF-32 encoding, macOS compatible, JIS2004 glyphs are default
UniJISX02132004-UTF32-V | Vertical version of UniJISX02132004-UTF32-H

### The Adobe-Japan2-0 Character Collection&mdash;DEPRECATED

The table below lists the supported encodings:

**Encoding** | **Byte Ranges**
--- | ---
ISO-2022 | 0x2121–0x7E7E
EUC-JP | 0x8FA1A1–0x8FFEFE
UCS-2 | 0x0000–0xD7FF & 0xE000–0xFFFF
UTF-8 | 0x00–0x7F, 0xC080–0xDFBF, 0xE08080–0xEFBFBF & 0xF0808080–0xF7BFBFBF
UTF-16 | 0x0000–0xD7FF, 0xE000–0xFFFF & 0xD800DC00–0xDBFFDFFF
UTF-32 | 0x00000000–0x0010FFFF

The table below provides descriptions of the Adobe-Japan2-0 CMap resources:

**CMap Resource** | **Description**
--- | ---
Adobe-Japan2-0 | Identity CMap
Hojo-H | JIS X 0212 character set, ISO-2022 encoding
Hojo-V | Vertical version of Hojo-H
Hojo-EUC-H | JIS X 0212 character set, EUC-JP encoding (code set 3)
Hojo-EUC-V | Vertical version of Hojo-EUC-H
UniHojo-UCS2-H | ISO/IEC 10646 (Unicode), UCS-2 encoding&mdash;OBSOLETE (use UniHojo-UTF16-H)
UniHojo-UCS2-V | Vertical version of UniHojo-UCS2-H&mdash;OBSOLETE (use UniHojo-UTF16-V)
UniHojo-UTF8-H | ISO/IEC 10646 (Unicode), UTF-8 encoding
UniHojo-UTF8-V | Vertical version of UniHojo-UTF8-H
UniHojo-UTF16-H | ISO/IEC 10646 (Unicode), UTF-16 encoding
UniHojo-UTF16-V | Vertical version of UniHojo-UTF16-H
UniHojo-UTF32-H | ISO/IEC 10646 (Unicode), UTF-32 encoding
UniHojo-UTF32-V | Vertical version of UniHojo-UTF32-H

### The Adobe-Korea1-2 Character Collection

The table below lists the supported encodings:

**Encoding** | **Byte Ranges**
--- | ---
ISO-2022 | 0x2121–0x7E7E
EUC-KR | 0x00–0x80 (0x00–0x83 in KSCpc), 0xFE–0xFF (only in KSCpc) & 0xA1A1–0xFEFE (0xA141–0xFDFE in KSCpc)
Johab | 0x00–0x80, 0x8441–0xD3FE, 0xD831–0xDEFE & 0xE031–0xF9FE
UHC | 0x00–0x80 & 0x8141–0xFEFE
UCS-2 | 0x0000–0xD7FF & 0xE000–0xFFFF
UTF-8 | 0x00–0x7F, 0xC080–0xDFBF, 0xE08080–0xEFBFBF & 0xF0808080–0xF7BFBFBF
UTF-16 | 0x0000–0xD7FF, 0xE000–0xFFFF & 0xD800DC00–0xDBFFDFFF
UTF-32 | 0x00000000–0x0010FFFF

The table below provides descriptions of the Adobe-Korea1-2 CMap resources:

**CMap Resource** | **Description**
--- | ---
Adobe-Korea1-0 | Identity CMap
Adobe-Korea1-1 | Identity CMap
Adobe-Korea1-2 | Identity CMap
KSC-H | KS X 1001 character set, ISO-2022-KR encoding
KSC-V | Vertical version of KSC-H
KSC-EUC-H | KS X 1001 character set, EUC-KR encoding, half-width KS X 1003 character set (nearly identical with ASCII) in one-byte range
KSC-EUC-V | Vertical version of KSC-EUC-H
KSC-Johab-H | KS X 1001 character set plus Johab extensions, Johab encoding
KSC-Johab-V | Vertical version of KSC-Johab-H
KSCms-UHC-H | KS X 1001 character set plus Microsoft extensions (Unified Hangul Code), UHC encoding
KSCms-UHC-V | Vertical version of KSCms-UHC-H
KSCpc-EUC-H | Apple Macintosh Korean character set, modified EUC-KR encoding, proportional-width ASCII character set in one-byte range
KSCpc-EUC-V | Vertical version of KSCpc-EUC-H
UniKS-UCS2-H | ISO/IEC 10646 (Unicode), UCS-2 encoding&mdash;OBSOLETE (use UniKS-UTF16-H)
UniKS-UCS2-V | Vertical version of UniKS-UCS2-H&mdash;OBSOLETE (use UniKS-UTF16-V)
UniKS-UTF8-H | ISO/IEC 10646 (Unicode), UTF-8 encoding
UniKS-UTF8-V | Vertical version of UniKS-UTF8-H
UniKS-UTF16-H | ISO/IEC 10646 (Unicode), UTF-16 encoding
UniKS-UTF16-V | Vertical version of UniKS-UTF16-H
UniKS-UTF32-H | ISO/IEC 10646 (Unicode), UTF-32 encoding
UniKS-UTF32-V | Vertical version of UniKS-UTF32-H

### The Adobe-KR-9 Character Collection

The table below lists the supported encodings:

**Encoding** | **Byte Ranges**
--- | ---
UTF-8 | 0x00–0x7F, 0xC080–0xDFBF, 0xE08080–0xEFBFBF & 0xF0808080–0xF7BFBFBF
UTF-16 | 0x0000–0xD7FF, 0xE000–0xFFFF & 0xD800DC00–0xDBFFDFFF
UTF-32 | 0x00000000–0x0010FFFF

The table below provides descriptions of the Adobe-KR-9 CMap resources:

**CMap Resource** | **Description**
--- | ---
Adobe-KR-0 | Identity CMap
Adobe-KR-1 | Identity CMap
Adobe-KR-2 | Identity CMap
Adobe-KR-3 | Identity CMap
Adobe-KR-4 | Identity CMap
Adobe-KR-5 | Identity CMap
Adobe-KR-6 | Identity CMap
Adobe-KR-7 | Identity CMap
Adobe-KR-8 | Identity CMap
Adobe-KR-9 | Identity CMap
UniAKR-UTF8-H | ISO/IEC 10646 (Unicode), UTF-8 encoding
UniAKR-UTF16-H | ISO/IEC 10646 (Unicode), UTF-16 encoding
UniAKR-UTF32-H | ISO/IEC 10646 (Unicode), UTF-32 encoding

## More Information

To learn more about CMap resources, please reference [Adobe Tech Note #5099](http://wwwimages.adobe.com/content/dam/Adobe/en/devnet/font/pdfs/5099.CMapResources.pdf), *Developing CMap Resources for CID-Keyed Fonts*, and [Adobe Tech Note #5014](http://wwwimages.adobe.com/content/dam/Adobe/en/devnet/font/pdfs/5014.CIDFont_Spec.pdf), *Adobe CMap and CID Font Files Specification*.

## Getting Involved

Send suggestions for changes to the CMap Resources project maintainer, [Dr. Ken Lunde](mailto:lunde@adobe.com?subject=[GitHub]%20CMap%20Resources), for consideration.
