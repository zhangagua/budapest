title: Ptmalloc Tcmalloc Jemalloc（一）
date: 2016-03-08 10:17:27
tags: 程序猿
categories: linux
---

早先我们曾经遇到一个很头疼的问题——在高并发、长时间的运行过程中，我们的程序出现了内存持续的增长，并导致程序因为Out Of Memory被系统kill掉。
一开始师兄们对自己的代码产生了怀疑，认为是代码里面有内存泄漏，先是使用valgrind去检测，然后又每个人挨个去回顾自己的代码，最后非常确认自己的代码是没有内存泄露的。唯一有疑问的地方是当时程序调用了opencv的接口。不过最后却不得不慢慢的开始怀疑我们到glibc上面的PtMalloc头上。
多番搜索之后找到了一篇来自淘宝网花名华庭的一份PtMalloc的分析报告，分析得非常精细和透彻。上到内存布局，下到PtMalloc的变量，整个PtMalloc尽收眼底。之后，又去找了一些相关的内存分配器来研究，看看各家异同。总的来说，最后被jeMalloc折服了。

---
## 内存布局
既然是内存分配器，那么首先要了解一个程序真正的逻辑内存分布格局。
![x86_64逻辑内存分布](/images/64_logical_mem.PNG)

Test Segment: 代码段
Data Segment: 已经初始化的全局变量段
BSS Segment: 未初始化的全局变量段
Heap: 堆区域
MMR: mmap映射区域
Stack: 栈区域（一个线程的栈大小一般为8M）
值得一提的就是，linux下面著名的Segment Fault出现的原因是对没有分配对应物理页的MMR或者Heap地址进行了操作。
*对于malloc一个ptmalloc并不能满足内存空间时，是延迟分配的。即在真正要使用该空间的时候，glibc才会从系统中去申请该物理空间,分配出来的逻辑空间才真正的和物理内存对应起来。*

---
## PtMalloc

| 名称        | 含义           |
| ----------- |:-------------:|
|Chunk       |Chunk是一个结构体，每一个Chunk代表一块内存  |
|Fast bins    |集中Chunk的索引结构，用于管理超小内存（默认阈值64B）|
|Bins        |集中Chunk的索引结构，用于管理既不属于超小内存也不属于超大内存的的部分，可细分为Unsorted bin，small bin，large bin三个部分      |
|Last remainder|分裂为small bin的一个cached的Chunk，主要目的是加速较小内存分配|
|Top Chunk    |特殊的Chunk，由于内存的分配是由低到高，那么在分配去的最顶部Chunk即为该分配去的Top Chunk|
|Mmapped Chunk|超过某个阈值的超大内存块均为此类特殊Chunk，这类Chunk直接使用系统调用获得，free时也会直接还给系统|
&emsp;&emsp;除了上面表格中的概念，还有两个概念是非常重要的——主分配区和非主分配区。每一个分配区都包含了自己的Fast bins、Bins、Last remainder、 Top Chunk。主分配区只有一个，非主分配区可以有多个。在程序一开始时，仅仅只有一个主分配区，当多个线程争用主分配区分配非特大内存时，ptmalloc会创建非主分配区来模拟和替代主分配区的功能，以加快在多线程环境下非特大内存的分配速度。我们一般很笼统的说，使用malloc分配内存，获得的内存是在堆上的。但是这个所谓的堆其实包括了上面内存分布图中的heap和mmap映射区域。mmap映射区域的内存按其作用可以分为两类，第一类是Mmap Chunk所管理的特大内存，另一类是非主分配区为了模拟主分配区的heap而使用的称为sub_heap的内存块。
&emsp;&emsp;先理一下ptmalloc分配内存的流程。

![ptmalloc flow chart](/images/ptmalloc_flow_chart.jpg)

&emsp;&emsp;ptmalloc持有的都是非超大内存（因为超大内存都是直接从系统中获得，释放时直接归还系统的），那么如果一段程序导致ptmalloc持有大量内存不归还操作系统，那么原因肯定在于非超大内存的管理上面。有两个很关键的问题，第一是超大内存和非超大内存的划分标准是什么?第二是非超大内存什么时候才会从索引管理中剔除，归还内存给OS。对大内存小内存的判断通过nmap_threshold来决定的。同时mmap_threshold默认是会动态变化的。

```c++
struct malloc_par{
…
unsigned long trim_threshold;/*收缩阈值，top chunk超过这个值会进行收缩，归还部分内存至操作系统*/
INTERNAL_SIZE_T mmap_threshold;/*分配阈值，是否属于特大内存划分的标准*/
…
}
/*ptmalloc默认开启分配阈值动态更新，mmap_threshold属于[128K,32M]区间
 *trim_threshold = mmap_threshold × 2
 */
```
&emsp;&emsp;前面已经提到过了，对特大内存的分配和回收都是直接从操作系统中来，到操作系统中去，不会持有在c库中。但是在特大内存回收中，有这样一段代码
![特大内存回收代码](/images/bigmem_recall.jpg)
特大内存的回收，会直接影响到对内存分类的阈值。再看非特大内存的分配和回收。
 - 非特大内存的分配（简化的流程）：先获得分配区，然后尝试从该区Fast bins和Bins中去获得合适的内存，若失败，则尝试从该区Top Chunk中分裂一部分出来，若是Top Chunk也不够大了，那么就拓展heap（或者是sub_heap）来使Top Chunk大到足够分裂。
 - 非特大内存的回收：
   1. 如果释放chunk与top chunk相邻，那么将它并入top chunk中去，进行第三步处理。否则进行第二步处理。
   2. 判断chunk 的大小和位置，由此决定将chunk进行相应回收处理和归入相应的索引结构中去，并不会归还给OS。
   3. 判断top chunk 的大小，如果超过了收缩阈值trim_threshold，则将其中一部分归还给OS；

&emsp;&emsp;也就是说，如果临近Top Chunk的Chunk一直被占有，那么Top Chunk会一直达不到收缩阈值。设想这样一种场景：我们首先malloc一个31M的空间，此时mmap_threshold变为了31M（意味着超过31M的属于特大内存）；紧接着，我malloc操作100次，每次30M，然后我释放了前面99次的内存；此时，最靠近Top Chunk的一个Chunk并没有被释放，于是整个ptmalloc此时就持有了将近2970M的空间。这也就很好的解释了ptmalloc持有大量内存的现象了。更由于ptmalloc在获取分配区时采用的搜索策略是：优先尝试获得之前已经使用过的分配区的试用锁，（在多线程环境下并不是一个非常友好的策略），所以它表现出来的性能并不很好。
&emsp;&emsp;之所以是这样，也是有原因的。mmap_threshold属于[128K,32M]区间，是因为当时从32位的机器到了64位的机器，逻辑空间扩大了很多，可是实际的物理内存扩大却不多，平时的服务器也就是64G或者更少。ptmalloc作者采用这个空间可能更多考虑了64位到32位逻辑空间的扩大，而没有衡量实际物理内存扩张速度较慢。还就是，ptmalloc 起源于 Doug Lea 的 malloc，由Wolfram Gloger实现多线程，用于 GLIBC 之中。由于它一开始仅仅是适用于单线程，被改成多线程的时候，多多少少还是有别扭的地方。


   







