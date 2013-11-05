#ifndef PTR_ARRAY_H
#define PTR_ARRAY_H

#include <stdint.h>

int ptr_array_add(void ***array,
                  uint32_t *count,
                  const void *ptr);

int ptr_array_add_nodup(void ***array,
                        uint32_t *count,
                        const void *ptr);

int ptr_array_contains(const void **array,
                       const uint32_t count,
                       const void *ptr);

#endif /* PTR_ARRAY_H */
