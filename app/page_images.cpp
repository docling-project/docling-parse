#include <cxxopts.hpp>

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>
#include <qpdf/Buffer.hh>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static void write_file(fs::path const& p, std::shared_ptr<Buffer> const& buf)
{
    std::ofstream out(p, std::ios::binary);
    if (!out)
    {
        throw std::runtime_error("unable to open output file: " + p.string());
    }
    out.write(reinterpret_cast<char const*>(buf->getBuffer()),
              static_cast<std::streamsize>(buf->getSize()));
}

static std::vector<std::string> get_filters(QPDFObjectHandle& stream)
{
    std::vector<std::string> filters;
    if (!stream.isStream())
        return filters;

    QPDFObjectHandle dict = stream.getDict();
    if (!dict.hasKey("/Filter"))
        return filters;

    QPDFObjectHandle f = dict.getKey("/Filter");
    if (f.isName())
    {
        filters.push_back(f.getName());
    }
    else if (f.isArray())
    {
        for (auto const& item : f.getArrayAsVector())
        {
            if (item.isName())
                filters.push_back(item.getName());
        }
    }
    return filters;
}

static std::string pick_extension(std::vector<std::string> const& filters,
                                  bool decoded_stream)
{
    if (!decoded_stream)
    {
        for (auto const& f : filters)
        {
            if (f == "/DCTDecode") return ".jpg";
            if (f == "/JPXDecode") return ".jp2";
            if (f == "/JBIG2Decode") return ".jb2";
        }
    }
    return ".bin";
}

int main(int argc, char* argv[])
{
    try
    {
        cxxopts::Options options("page_images", "Extract images from PDF pages");

        options.add_options()
            ("i,input", "Input PDF file", cxxopts::value<std::string>())
            ("o,output", "Output directory (default: ./images_out)", cxxopts::value<std::string>()->default_value("./images_out"))
            ("p,page", "Page number to process (default: -1 for all)", cxxopts::value<int>()->default_value("-1"))
            ("m,mode", "Stream mode: raw or decoded (default: raw)", cxxopts::value<std::string>()->default_value("raw"))
            ("h,help", "Print usage");

        auto result = options.parse(argc, argv);

        if (result.count("help") || !result.count("input"))
        {
            std::cout << options.help() << std::endl;
            return result.count("help") ? 0 : 1;
        }

        fs::path in_pdf = result["input"].as<std::string>();
        fs::path out_dir = result["output"].as<std::string>();
        int target_page = result["page"].as<int>();
        bool want_decoded = (result["mode"].as<std::string>() == "decoded");

        fs::create_directories(out_dir);

        QPDF pdf;
        pdf.processFile(in_pdf.string().c_str());

        QPDFPageDocumentHelper dh(pdf);
        auto pages = dh.getAllPages();

        int global_img_index = 0;

        for (size_t page_idx = 0; page_idx < pages.size(); ++page_idx)
        {
            if (target_page >= 0 && static_cast<int>(page_idx) != target_page)
                continue;

            QPDFPageObjectHelper page = pages.at(page_idx);

            page.forEachImage(
                true,
                [&](QPDFObjectHandle& img, QPDFObjectHandle& /*xobj_dict*/, std::string const& key)
                {
                    if (!img.isStream())
                        return;

                    auto filters = get_filters(img);

                    std::shared_ptr<Buffer> data;
		    //PointerHolder<Buffer> data;
                    bool wrote_decoded = false;

                    if (want_decoded)
                    {
                        try
                        {
                            data = img.getStreamData();
                            wrote_decoded = true;
                        }
                        catch (...)
                        {
                            data = img.getRawStreamData();
                            wrote_decoded = false;
                        }
                    }
                    else
                    {
                        data = img.getRawStreamData();
                        wrote_decoded = false;
                    }

                    std::string ext = pick_extension(filters, wrote_decoded);

                    std::string safe_key = key;
                    for (char& c : safe_key)
                    {
                        if (c == '/' || c == '\\' || c == ':' || c == '*'
                            || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
                            c = '_';
                    }

                    fs::path out_path = out_dir / (
                        "page_" + std::to_string(page_idx + 1) +
                        "_xobj_" + safe_key +
                        "_img_" + std::to_string(++global_img_index) +
                        (wrote_decoded ? "_decoded" : "_raw") +
                        ext);

                    write_file(out_path, data);

                    std::cout << "wrote " << out_path.string()
                              << " (" << data->getSize() << " bytes"
                              << (wrote_decoded ? ", decoded" : ", raw") << ")\n";
                });
        }

        return 0;
    }
    catch (cxxopts::exceptions::exception const& e)
    {
        std::cerr << "Error parsing options: " << e.what() << "\n";
        return 1;
    }
    catch (std::exception const& e)
    {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
