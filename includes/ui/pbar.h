#ifndef KBASE_PBAR_H
#define KBASE_PBAR_H

#include <stddef.h>
#include <stdint.h>

void term_size(uint64_t* rows, uint64_t* cols);
void progress_bar(const char* label, size_t completed, size_t total);

#endif // KBASE_PBAR_H
