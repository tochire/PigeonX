#ifndef WORKER_H
#define WORKER_H

#include <unordered_map>
#include <vector>
#include "smtp_logic.h"
#include "types.h"
#include <fcntl.h>



void handle_readable(Worker& w, int fd);
void worker_loop(Worker* wptr, int id);

// Make fd non-blocking
 int make_nonblocking(int fd);


#endif // WORKER_H
