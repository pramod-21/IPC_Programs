Implement a program where multiple worker processes update counters in shared memory and a master process aggregates results.

In the program multiple worker processes will update counters in shared memory and master process aggregates the result

Example: master + worker processes updating counters in System V shared memory
* - Uses shmget/shmat to create shared memory between parent and children
* - Uses a System V semaphore set to protect a global counter
* - Each worker also updates its own per-worker counter in shared memory (so master can aggregate)
*
Commands to build and run code
* Build: gcc -o shared_counters_sysv shared_counters_sysv.c
* Run: ./shared_counters_sysv <num_workers> <num_increments_per_worker>
* Example: ./shared_counters_sysv 4 100000
*/

