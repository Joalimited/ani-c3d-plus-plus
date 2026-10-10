// Extra manga sources for when MangaDex only hosts part of a series (or none of it): MangaPill, MangaKatana and MangaRead.
// A book id is `<prefix>:<path>` ("mp:723/chainsaw-man", "mk:chainsaw-man.21890"); page images need a Referer.
#pragma once
#include <map>
#include <string>
#include <vector>

namespace mangasrc {
struct Item {
    std::string id, title, cover, src;
};
struct Chapter {
    std::string id, chapter, src;  // id is a url path (mp) or a full url (mk / mr)
};
extern const std::string UA, PILL, KATANA, READ;
extern const std::map<std::string, std::string> LABELS;

std::vector<Item> search(const std::string& query, int n = 10);
std::vector<Chapter> chapters(const std::string& book_id);
std::vector<std::string> pages(const Chapter& ch);
std::string referer(const std::string& url);  // "" when none is needed
}  // namespace mangasrc
