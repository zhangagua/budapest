title: Ptmalloc Tcmalloc Jemalloc（三）
date: 2016-03-26 09:25:58
tags: 程序猿
categories: linux
---

在项目中我们一开始使用的ptmalloc，再然后使用tcmalloc，都存在库持有大量内存不归还操作系统的情况。我们计算过，从数据库中读出的数据在采用了我们的（列式）存储结构之后，在内存中的大小是预估的原始大小的5倍开外（ptmalloc和tcmalloc差别不大，该列为无重复数据，没有压缩）。在那个时候，我们都已经将实习一版自己的malloc或者说自己设计、实现内存池提上了日程，直到我们使用了JeMalloc之后。对于多数列来说，JeMalloc基本都是在3倍以内，对于我们来说，这个大小和理论上计算的大小基本一致。符合了我们不追内存分配速度，追求内存的精简基本需求。

## JeMalloc相关概念

先把JeMalloc中涉及的概念都提出来，这样思路清晰很多。
>Arena：和之前的ptmalloc和tcmalloc一样，都是指线程分配区。
>Chunk：以4M为大小单位的内存
>Run：管理large和small两个级别的内存块的管理索引
>Huge Object：超大内存块，一般由多个Chunk组成，归还时不会自己持有，会直接归还给操作系统。
>Large Object：大内存块，一个Run如果要管理打内存块，那就是一一对应的关系，一个Run就是一个大内存块。
>Small Object:小内存块，一个Run可以管理多个Small内存块。
>bin:用来管理各个不同大小单元的分配，比如最小的Bin管理的是8字节的分配，每个Bin管理的大小都不一样，依次递增。jemalloc的bin和ptmalloc的bin的作用类似。
>tcache:线程对应的私有缓存空间,在分配内存时首先从tcache中找，miss的情况下才会进入一般的分配流程。

一个一个说一下，一个Arena个数默认为4*核心数，一个线程固定使用一个Arena，线程初始时采用round—robin的方法来分配。它包含控制信息+若干的chunk，独立的为该线程中的内存请求做内存管理。Arena在向操作系统申请内存的时候，都是以Chunk为单位去申请。关于run，大家看下图（直接从fb博客上截的）,应该很明了了。注意观察large run和small run。
![jemalloc_run](/images/jemalloc_run.jpg)

来自facebook的技术博客中有一个直观的统计，大家可以对大小有一个概念:
>Small: [8], [16, 32, 48, ..., 128], [192, 256, 320, ..., 512], [768, 1024, 1280, ..., 3840]
>Large: [4 KiB, 8 KiB, 12 KiB, ..., 4072 KiB]
>Huge: [4 MiB, 8 MiB, 12 MiB, ...]

Huge内存的释放时直接归还OS，由一颗红黑树来管理，以前是全局一颗，4.0版本以后也是划归到了Arena，由一个线程自己的Huge红黑书。

## 串联
![jemalloc_run](/images/jemalloc_overview.jpg)

由上图可知，一个Arena含有多个bin，每一个bin都会记录当前的run(run_cur),以及一个由多个run组成的一颗红黑树（runs）。run本身由多个物理页组成，使用底地址优先的分配策略。chunk默认是4M，而run是在chunk中进行实际分配的操作对象。在分配时，如果没有对应的run存在就要新建一个，哪怕只分配一个块，比如只申请一个8字节的块，也会生成一个大小为一个page（默认4K）的run。下面这里有分配流程图，画的有点奇怪，不过细看一下还是能看明白的。

![jemalloc_alloc](/images/jemalloc_alloc.png)

