fastsum
=======

This is a multi-threaded two-level SHA256 checksum calculator. It uses some tricks to compute
SHA256 efficiently (more to come), reads blocks from disk in parallel, computes
hashes in parallel in 16kB blocks and then puts the results together by hashing
all partial hash results belonging to a particular file.

The resulting checksum is therefore a SHA256 hash of collected SHA256 hashes of each 16kB block
from the original file.

By default, the number of hash worker threads is the same as number of your CPU cores,
because hashing is a CPU-bound task. Number of file worker threads is 16,
on the assumption that reading small files in multiple threads allows the operating
system to reorder and service the requests more efficiently. The code also only allows
one thread at a time to read large files, on the assumption that sequentially reading
a large file is faster than interrupting the sequential read by seeks elsewhere.

(For purposes of fastsum, "large file" is anything over 1MB. This should probably
be configurable.)

If you use a traditional spinning drive, your read speeds are going to be so low that
the whole task will be I/O bound. Fastsum is probably useless for you, a plain sha256sum
will suffice. If you use a SSD, you can take advantage of the parallel hashing technique.
If you're using a fast RAID array of spinning drives, you might get high read speeds
combined with high seek latency, which is where the multithreaded reading should shine.
(it is untested at the moment)


Code Overview
-------------

`queue.c` is an implementation of a fixed-size (or growable) producer-consumer queue.
This uses mutex+semaphores to synchronize access.

`sha256.c`, predictably, implements the SHA256 hash. Unlike other implementations,
this can only work if you supply the whole block to be hashed in advance.

`tools.c` is a stupid collection of useful functions, namely xmalloc ("die if you run
out of memory because what else you expect to do?")

`main.c` is where the magic happens.

The program uses three queues and corresponding sets of worker threads:

1. `file_queue`, which contains a list of files to be processed
2. `hash_queue`, which contains work blocks for the hash threads
3. `completed_queue`, which handles tasks that are finished, by assigning hashed blocks
   to files and printing results or errors

The main thread scans all files passed on the command line, recursing into directories
if necessary. Each file is added to the `file_queue`, to be picked up by a file worker.

The file worker's job is to read the file in 16kB chunks and submit these into the
`hash_queue`, where they are picked up by the hash workers. When all hash-work is
submitted, or when an error occurs, then the file task is passed to the completion queue.

Hash workers simply hash the passed chunks, store the results in specified locations,
and submit the finished task to the completion queue.

Completion worker collects the hash results. When all chunks for a particular file
are posted, it generates a new hashing task on top of all the partial results
and submits that back to the `hash_queue`. When the second-level hash is done,
completion worker prints the result. It also prints error messages. (This is for
two reasons: one, we want to print all errors in a single thread so that they don't
get interleaved. Two, if an error occurs after some number of chunks have been posted,
it's still necessary to collect the posted chunks, so the completion worker still
needs to know about them.)

`completed_queue` is growable, in order to prevent saturation deadlocks between it
and `hash_queue`: hash workers post into completion queue, and completion worker
posts into hash queue. There can arise a situation when the hash queue is full
and completion worker waits until it frees up, but in the meantime, the hash workers
are trying to post to an already full completion queue. Therefore, at least one
of the queues must never block. And it is the completion queue.

Size of the hash queue imposes a soft limit on total memory consumption: 16kB blocks
times 16 384 entries in the queue. This is not a hard limit, as finished tasks
get posted to the completion queue, where they can spend considerable time before
being freed. If we wanted to enforce the memory limit this way, however, we could let
hash workers to free the blocks before posting to completion queue.
