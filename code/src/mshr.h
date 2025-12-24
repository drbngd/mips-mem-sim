#ifndef _MSHR_H_
#define _MSHR_H_

#include <cstdint>

struct MSHR {
    bool valid;
    uint32_t address; /* Block Aligned Address */
    bool is_write;
    bool done;        /* Set true when data returns from memory */
    uint32_t ready_cycle; /* Cycle when data is ready to be used */

    MSHR() : valid(false), address(0), is_write(false), done(false), ready_cycle(0) {}
};

#endif
