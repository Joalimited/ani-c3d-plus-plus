#include "pdf.hpp"

#include "paths.hpp"
#include "strutil.hpp"

namespace pdf {
bool write_images(const std::string& path, const std::vector<Image>& pages, double dpi, int quality) {
    std::string out = "%PDF-1.4\n%\xE2\xE3\xCF\xD3\n";
    std::vector<size_t> offsets;
    auto obj = [&](const std::string& body) {
        offsets.push_back(out.size());
        out += std::to_string(offsets.size()) + " 0 obj\n" + body + "\nendobj\n";
    };
    int n = (int)pages.size();
    // object numbers: 1 catalog, 2 pages, then (page, image, contents) per page
    std::string kids;
    for (int i = 0; i < n; i++) kids += std::to_string(3 + i * 3) + " 0 R ";
    obj("<< /Type /Catalog /Pages 2 0 R >>");
    obj("<< /Type /Pages /Kids [" + kids + "] /Count " + std::to_string(n) + " >>");
    for (int i = 0; i < n; i++) {
        const Image& im = pages[i];
        double wpt = im.w * 72.0 / dpi, hpt = im.h * 72.0 / dpi;
        int page_no = 3 + i * 3, img_no = page_no + 1, cont_no = page_no + 2;
        obj(su::fmt("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 %.2f %.2f] /Resources << /XObject << /Im0 %d 0 R >> >> /Contents %d 0 R >>", wpt, hpt, img_no, cont_no));
        std::string jpg;
        if (!img::save_jpeg("", im, quality, &jpg)) return false;
        offsets.push_back(out.size());
        out += std::to_string(offsets.size()) + " 0 obj\n" +
               su::fmt("<< /Type /XObject /Subtype /Image /Width %d /Height %d /ColorSpace /DeviceRGB /BitsPerComponent 8 /Filter /DCTDecode /Length %zu >>\nstream\n", im.w, im.h,
                       jpg.size()) +
               jpg + "\nendstream\nendobj\n";
        std::string content = su::fmt("q %.2f 0 0 %.2f 0 0 cm /Im0 Do Q", wpt, hpt);
        obj("<< /Length " + std::to_string(content.size()) + " >>\nstream\n" + content + "\nendstream");
    }
    size_t xref = out.size();
    out += "xref\n0 " + std::to_string(offsets.size() + 1) + "\n0000000000 65535 f \n";
    for (size_t off : offsets) out += su::fmt("%010zu 00000 n \n", off);
    out += "trailer\n<< /Size " + std::to_string(offsets.size() + 1) + " /Root 1 0 R >>\nstartxref\n" + std::to_string(xref) + "\n%%EOF\n";
    paths::makedirs(paths::dirname(path));
    return paths::write_file(path, out);
}
}  // namespace pdf
