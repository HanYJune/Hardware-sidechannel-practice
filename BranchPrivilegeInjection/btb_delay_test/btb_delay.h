#pragma once
void set_jump_table(unsigned long src_addr, unsigned long dst_addr);

void clear(unsigned long src_addr, size_t src_snip_size, unsigned long dst_addr, size_t dst_snip_size);

void run_all_num_of_ops();
