#ifndef READER_BOOK_PDF_H
#define READER_BOOK_PDF_H

#include "book.h"

namespace reader {

bool pdf_summary(const std::filesystem::path& path, Book& book);
bool load_pdf_window(Book& book, int center, int radius);
void ensure_pdf_index(Book& book);

}  // namespace reader

#endif
