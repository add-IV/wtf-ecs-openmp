# WTF-ECS

just use openmp

two lines to get 5x speedup (line 1427 and 1450 in [ecs.c](src/ecs.c#L1427))

smh

```
freq: 0.000046
singly-threaded: 3.810000s
ecs_table.size: 65514
alt singly-threaded: 1.000000s
ecs_table.size: 65514
threads: 8
multi-threaded1: 14.400000s
ecs_table.size: 65514
multi-threaded2: 16.460000s
ecs_table.size: 65514
POSIX multi-threaded: 16.680000s
ecs_table.size: 65514
alt multi-threaded: 3.580000s
ecs_table.size: 65514
other alt multi-threaded: 2.900000s
ecs_table.size: 65514
openmp: 0.230000s <---- look here
ecs_table.size: 65514
```
