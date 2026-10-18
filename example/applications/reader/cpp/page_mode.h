#ifndef READER_PAGE_MODE_H
#define READER_PAGE_MODE_H

#include "session.h"

namespace reader {

bool mode_page_checked(const Session& session);
void ensure_measure_host(Session& session);
void invalidate_page_cache(Session& session);
int measure_pages(Session& session, int chapter_index);
void paint_page(Session& session, int chapter_index, int page_index, int dir = 0);
void turn_page(Session& session, int dir);
void show_in_page_mode(Session& session, int chapter_index, int page_hint = 0);
void sync_mode_from_dom(Session& session);

}  // namespace reader

#endif
