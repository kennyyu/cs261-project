#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "ptr_array.h"

/**
 * Add a pointer to an array of pointers.
 *
 * @param[in,out] array      array to add to
 * @param[in,out] count      count of elements in array
 * @param[in]     ptr        pointer to add
 *
 * @returns       0 on success
 */
int
ptr_array_add(void ***array, uint32_t *count, const void *ptr)
{
   void                     *tmparray;
   size_t                    newsize;

   newsize = (*count + 1) * sizeof(**array);
   tmparray = realloc(*array, newsize);

   if ( tmparray == NULL ) {
      free(*array), *array = NULL;
      fprintf( stderr, "ptr_array_add: realloc failed %s\n",
               strerror(errno) );

      return -ENOMEM;
   }

   *array = tmparray;

   (*array)[(*count)++] = (void *)ptr;

   return 0;
}

/**
 * Add a pointer to an array of pointers if it is not already there.
 *
 * @param[in,out] array      array to add to
 * @param[in,out] count      count of elements in array
 * @param[in]     ptr        pointer to add
 *
 * @returns       0 on success
 */
int
ptr_array_add_nodup(void ***array, uint32_t *count, const void *ptr)
{
   if ( (*array != NULL) &&
        ptr_array_contains((const void **)*array, *count, ptr) )
   {
      return 0;
   }

   return ptr_array_add(array, count, ptr);
}

/**
 * Does the array contain a given pointer?
 *
 * @param[in,out] array      array to search
 * @param[in,out] count      count of elements in array
 * @param[in]     ptr        pointer to look for
 *
 * @retval        1          array contains ptr
 * @retval        0          array does NOT contain ptr
 */
int
ptr_array_contains(const void **array, const uint32_t count, const void *ptr)
{
   uint32_t                  entry;

   for (entry = 0; entry < count; entry++) {
      if (array[entry] == ptr) {
         return 1;
      }
   }

   return 0;
}
