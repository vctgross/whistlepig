#include <stdio.h>
#include "whistlepig.h"

#define isempty(flag, i) ((flag[i>>4]>>((i&0xfU)<<1))&2)
#define isdel(flag, i) ((flag[i>>4]>>((i&0xfU)<<1))&1)
#define KEY(h, i) &(h->pool[h->keys[i]])

RAISING_STATIC(dump_posting_list(wp_segment* s, posting_list_header* plh)) {
  posting po;
  docid_t last_doc_id = 0;
  int started = 0;

  uint32_t offset = plh->next_offset;
  printf("[%u entries]\n", plh->count);

  while(offset != OFFSET_NONE) {
    RELAY_ERROR(wp_segment_read_posting(s, offset, &po, 1));

    printf("  @%u doc %u:", offset, po.doc_id);
    for(uint32_t i = 0; i < po.num_positions; i++) printf(" %d", po.positions[i]);

    if((po.doc_id == 0) || (started && (po.doc_id >= last_doc_id))) printf(" <-- BROKEN");
    started = 1;
    last_doc_id = po.doc_id;
    printf("\n");

    offset = po.next_offset;
    free(po.positions);
  }

  return NO_ERROR;
}

RAISING_STATIC(dump_label_posting_list(wp_segment* s, posting_list_header* plh)) {
  posting po;
  docid_t last_doc_id = 0;
  int started = 0;

  uint32_t offset = plh->next_offset;
  printf("[%u entries]\n", plh->count);

  while(offset != OFFSET_NONE) {
    RELAY_ERROR(wp_segment_read_label(s, offset, &po));

    printf("  @%u doc %u", offset, po.doc_id);
    if((po.doc_id == 0) || (started && (po.doc_id >= last_doc_id))) printf(" <-- BROKEN");
    started = 1;
    last_doc_id = po.doc_id;
    printf("\n");

    offset = po.next_offset;
    free(po.positions);
  }

  return NO_ERROR;
}

RAISING_STATIC(dump(wp_segment* segment)) {
  termhash* th = MMAP_OBJ(segment->termhash, termhash);
  stringmap* sh = MMAP_OBJ(segment->stringmap, stringmap);
  stringpool* sp = MMAP_OBJ(segment->stringpool, stringpool);

  uint32_t* thflags = TERMHASH_FLAGS(th);
  term* thkeys = TERMHASH_KEYS(th);
  posting_list_header* thvals = TERMHASH_VALS(th);

  for(uint32_t i = 0; i < th->n_buckets; i++) {
    if(isempty(thflags, i)); // do nothing
    else if(isdel(thflags, i)) printf("%u: [deleted]", i);
    else {
      term t = thkeys[i];

      if(t.field_s == 0) { // sentinel label value
        if(t.word_s == 0) { // sentinel dead list value
          printf("%u: (dead list)\n", i);
        }
        else {
          const char* label = stringmap_int_to_string(sh, sp, t.word_s);
          printf("%u: ~%s\n", i, label);
        }
        RELAY_ERROR(dump_label_posting_list(segment, &thvals[i]));
      }
      else {
        const char* field = stringmap_int_to_string(sh, sp, t.field_s);
        const char* word = stringmap_int_to_string(sh, sp, t.word_s);
        printf("%u: %s:'%s'\n", i, field, word);
        RELAY_ERROR(dump_posting_list(segment, &thvals[i]));
      }
    }
  }

  return NO_ERROR;
}

int main(int argc, char* argv[]) {
  if(argc != 2) {
    fprintf(stderr, "Usage: %s <segment filename>\n", argv[0]);
    return -1;
  }

  wp_index* index;
  DIE_IF_ERROR(wp_index_load(&index, argv[1]));
  DIE_IF_ERROR(wp_index_dumpinfo(index, stdout));

  for(int i = 0; i < index->num_segments; i++) {
    printf("\nsegment %d details:\n", i);
    DIE_IF_ERROR(dump(&index->segments[i]));
  }

  DIE_IF_ERROR(wp_index_unload(index));

  return 0;
}

