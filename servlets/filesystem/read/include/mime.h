#ifndef __MIME_H__
#define __MIME_H__

typedef struct _mime_map_t mime_map_t;

mime_map_t* mime_map_new(const char* spec_file);

int mime_map_free(mime_map_t* map);

const char* mime_map_query(const char* ext_name);
#endif
