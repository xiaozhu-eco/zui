#ifndef READER_BOOK_EPUB_H
#define READER_BOOK_EPUB_H

#include "book.h"

namespace reader {

bool epub_summary(const std::filesystem::path& path, Book& book);
bool load_epub_window(Book& book, int center, int radius);
void ensure_epub_index(Book& book);

}  // namespace reader

#endif
