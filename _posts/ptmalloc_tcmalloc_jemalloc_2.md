title: Ptmalloc Tcmalloc Jemalloc（二）
date: 2016-03-10 10:19:57
tags: 程序猿
categories: linux
---

# Tcmalloc

tags： 程序猿

---

&emsp;&emsp;之前翻译过Tcmalloc官方文档中的总揽章，既没有过分追求细节，又解释了Tcmalloc最为关键的地方，非常适合对Tcmalloc有一个初步的了解。先把它贴出来，感觉有的地方翻译过来怪怪的，最后再总结一下Tcmalloc。(后面再去看了之前的页面，发现比之前有一些更新了。)

---

##总揽：
&emsp;&emsp;TCmalloc会给每个线程分配一个线程cache，小的内存分配一般能够在这个线程cache中获得需要的内存。如果有需要，内存会从中央堆分配到线程cache中；中央堆会定期回收线程cache中的内存。
&emsp;&emsp;256K为阈值区作为大小内存的划分。大内存的分配实在中心堆上，以页为单位（页为8K对齐的）。换而言之，大内存的分配永远都是以页为单位，通常是一个整数数量的页。
一个被使用的页面可以被分成多个等大的小对象。例如，一个4K的页面，能够被分成32个128bytes的对象。
![tcmalloc_overview](/images/tcmalloc_overview.gif)


###小内存对象的分配：
&emsp;&emsp;每个小内存对象会根据其大小被分类，tcmalloc一共有170（以前写的是88）种小内存对象类型。例如，大小在961到1024bytes的内存对象都会被分到1024这个类型。每种类型都是间隔的，或许间隔为8bytes或者16或者32。不过类型之间最大的间隔必须被控制，不然内存浪费会剧增。（旧一点的版本写的是，<=32KB的对象被称作是小对象，>32KB的是大对象。在小对象中，<=1024bytes的对象以8n bytes分配，1025<size<=32KB的对象以128n bytes大小分配，比如：要分配20bytes则返回的空闲块大小是24bytes的，这样在<=1024的情况下最多浪费7bytes，>1025则浪费127bytes。而大对象是以页大小4KB进行对齐的，最多会浪费4KB - 1 bytes。新版本肯定发生了变化，具体还要再细看）
&emsp;&emsp;每一个线程cache都有一个自己的单链表，里面放着未被使用的，各种大小类型的内存块。
&emsp;&emsp;分配小内存的过程:
&emsp;&emsp;&emsp;&emsp;	1. 找到其对应的大小类型
&emsp;&emsp;&emsp;&emsp;	2. 查看当前线程cache的单链表
&emsp;&emsp;&emsp;&emsp;	3. 如果链表不为空，那么摘除第一个块，然后返回它。在这一过程中，tcmalloc完全不需要锁（这个内存cache是针对单个线程的，当然不需要锁）。这加速了分配blablabla。
&emsp;&emsp;&emsp;&emsp;	4. 如果它为空（这个级别的内存块用完了哟）：a.从中央堆的链表中取出对应大小类型的内存块 b.将它放到线程cache空闲链表中 c.将刚获得的内存块返回给应用。（必然加锁）
&emsp;&emsp;&emsp;&emsp;5. 如果中央堆中的链表也为空：(1)我们将在页分配器上去获取新的页(2)将新的页分成该级别的大小类型(3)将新的内存块放入中央链表(4)像之前说的一样，把一些该级别的内存块放到线程cache链表中。（还有第五步，就是把第一个内存块返回应用）（必然加锁）
![tcmalloc_threadheap](/images/tcmalloc_threadheap.gif)
&emsp;&emsp;思考：为什么要有一个中央堆？如果没有中央堆，线程cahce链表中没有该类型时，直接使用页分配器分配页呢？中央堆存在的意义就是内存缓存，页分配器的速度必然是比直接在中央堆获得要慢的。中央堆的存在，能够加速在线程cache中的内存未命中。

###量定线程cache的大小：
&emsp;&emsp;线程cache的大小很重要，如果太小那么会导致频繁访问中心堆，如果太大那么会导致大量内存浪费。
&emsp;&emsp;需要注意到线程cache对于分配和释放内存块同样重要。如果没有线程cache那么内存块的回收也会访问中央堆（加锁）。但是，有些线程的表现是不对称的(比如生产者消费者模型，生产者只负责分配，消费者只负责回收)。所以量定cache的大小变得更棘手。
&emsp;&emsp;为了量定cache的大小，我们使用了一个慢启动的算法来确定每个独立的线程cache（也就是那个放空闲块的链表）的最大大小（每个线程cache的大小都是单独计算的）。如果线程cache被频繁使用，那么它的最大长度变大。尽管如此，当一个空闲链表更多的是用于回收内存的时候，它会增长到一个点（阈值），当链表大小增长到该阈值，list中的多个内存块会被移动到中央堆。
&emsp;&emsp;下面的代码描述了慢启动算法。注意到 num_objects_to_move对于每个线程都是特定的。通过一个共享的length，中央堆能够效率的在各个线程之间传递内存块。如果一个线程想要比num_objects_to_move 更小，在中心堆上的操作是线性复杂度的。使用 num_objects_to_move 来决定与中心堆交互时块的数量的缺点是，线程cache不一定能够全部使用这些内存块，从而造成内存浪费。

```
Start each freelist max_length at 1.

Allocation
  if freelist empty {
    fetch min(max_length, num_objects_to_move) from central list;
    if max_length < num_objects_to_move {  // slow-start
      max_length++;
    } else {
      max_length += num_objects_to_move;
    }
  }


Deallocation
  if length > max_length {
    // Don't try to release num_objects_to_move if we don't have that many.
    release min(max_length, num_objects_to_move) objects to central list
    if max_length < num_objects_to_move {
      // Slow-start up to num_objects_to_move.
      max_length++;
    } else if max_length > num_objects_to_move {
      // If we consistently go over max_length, shrink max_length.
      overages++;
      if overages > kMaxOverages {
        max_length -= num_objects_to_move;
        overages = 0;
      }
    }
  }
```

###大内存的分配：
&emsp;&emsp;大于256K的内存块会8K对齐，被中心页堆管理起来。中心页堆也是一个线性的数据链表。对于 i < 128，第k个位置进入的空闲链表，里面每一块内存都是k页。第128个位置里面的包含大于128页的内存块。
&emsp;&emsp;对于分配k页的求情，首先去找第k个入口，如果链表是空的，我们就找下一个入口（k+1，还是没有就找k+2）。最后，我们去找最后一个链表，如果也失败了，那么我们就需要去系统取内存了。使用 sbrk，mmap或者通过映射/dev/mem
&emsp;&emsp;如果一个k页的请求分配了一个k+1或者更多的页，归还的时候，要归还到适当的入口处。
![tcmalloc_pageheap](/images/tcmalloc_pageheap.gif)

###Spans:
&emsp;&emsp;Tcmalloc管理的堆，由一组页面组成。通过span对象来表示占有连续页面一个应用。Span可以被分配或者释放。如果是释放，span是某个页堆链的入口（？？？）。如果是分配，既可能是作为交给某个应用的大内存，也有可能作为运行页面被分成一系列的小内存对象。
如果是被划分成span，那么该大小类型的对象会在span中做记录。组织页面序号而成的中央数组用于找到组成一个span的那些页面。
![tcmalloc_spanmap](/images/tcmalloc_spanmap.gif)
&emsp;&emsp;在32位的地址空间中，中央数组被表示成一个 2-level radix tree（两层基数树？）。root有32个分支，每个叶子节点有2^14个入口（因为32位地址空间有2^19个8k页，第一层已经将2^19个入口分成了2^5个入口，所以叶子节点就是2^14个入口）。这导致了中央数组需要使用64K的空间。
	在64位机器上，我们使用3-level radix tree.

###回收：
&emsp;&emsp;当一个内存对象被回收时。我们计算他的页数量并且在中央数组中去查找它属于相应的span对象。Span会告诉我们它是否属于小内存对象，如果是，会告诉以及其大小类型。如果是小内存对象，它会被插入当前线程cache中合适大小的链中。如果线程cache超过了预定大小（2M），我们会调度回收器将线程cache调度到中央空闲链表中去（调度多少？调度哪些？）。
&emsp;&emsp;如果是一个大内存对象，span对象会告诉我们这个内存对象使用页的范围。假设这个范围是[p,q]。我们查看页p-1和q+1，如果相邻的span都是空闲的，我们将他们合并到span中去。结果span会被插入到相应的空闲页堆链表中去。

###中央空闲链表：
&emsp;&emsp;正如前面提到的，我们对每个大小类型的小内存对象缓存一个中央空闲链表。每个中央空闲链表的组织采用两层结构：span的集合，每个span有一个链表，存放空闲内存块。
&emsp;&emsp;一个对象从中央空闲链表中尝试分配，首先分配一些span空闲链表的第一个空闲块（如果有多个span都有空闲块，那么大小最合适的span会首先被分配出去）。
&emsp;&emsp;一个对象通过放入它所属的span的空闲链表中来完成归还动作。如果该链表的长度现在等于该span总共拥有块的数量（换言之就是整个span都是空闲未被使用的），那么就释放整个span，然后将它归还给页堆。

###对于线程cache的内存回收：
&emsp;&emsp;线程cache的垃圾收集使得线程cache处于控制中，并且能够将未使用的内存对象归还给中央空闲链表。有的线程需要大量的cache而有的cache完全不需要cache。当一个线程cache超过最大大小时，垃圾回收将加入，此后线程将和其他线程为了更大的cache而竞争（？？？）。
&emsp;&emsp;垃圾回收器仅仅在内存对象回收的时候才运行，我们将walk over all free lists（回收谁，归还谁，就扫描谁，这里使用walk over不合适吧），然后将一些空闲内存对象回收到中央空闲链表。
&emsp;&emsp;移动空闲对象的数量由per-list low-water-mark L决定。L记录了在最近一次垃圾回收之前该cache链的最短长度。(Note that we could have shortened the list by L objects at the last garbage collection without requiring any extra accesses to the central list)，注意到在上一次的垃圾回收中，可能已经通过L回收过该链了，既然回收了为什么不需要对中央链有额外的请求？我们通过历史预测未来，我们移动L/2的对象到到中央链中去。这个算法有很好的表现，一但一个线程不再使用某种大小类型的小内存对象，它就会被迅速的移动到中央空闲链表中去。
&emsp;&emsp;如果一个线程持续的回收内存，远超过它分配内存，这种策略引起一种现象：总有至少L/2个内存对象放在cache空闲链表中。为了避免这种内存浪费，我们将cache链的最大长度收缩到 num_objects_to_move

```
Garbage Collection
  if (L != 0 && max_length > num_objects_to_move) {
    max_length = max(max_length - num_objects_to_move, num_objects_to_move)
  }
```

&emsp;&emsp;实际上，当一个线程cache超过他的max_size（注意是大小不是个数），表明了该线程将受益于更大的cache。简单的增大max_size会使多个激活的线程内存过剩（???这个变量是全局的？并不是，属于线程）。开发者可以通过标志tcmalloc_max_total_thread_cache_bytes来限制（所有线程）内存的使用。
&emsp;&emsp;每个线程cache开始于一个小的大小（64KB），所以空闲的线程cache是不会预先分配他们并不会使用的内存。每次垃圾回收器运行的时候，他都会尽力扩大自己的max_size。如果所有线程的size总和少于tcmalloc_max_total_thread_cache_bytes，max_size可以非常容易的增大，但是如果到达了，线程1cache会尝试从线程2cache中偷取，缩小线程2cache的max_size。通过这种方式，更加活跃的线程会从没那么活跃的线程中偷的更多。绝大多数空闲线程终结于小内存，活跃线程终结于大内存。需要注意的就是这种偷取方式可以导致所有线程的总内存持有量超过tcmalloc_max_total_thread_cache_bytes，直到线程2归还内存时触发回收器。

---

&emsp;&emsp;Tcmalloc的最重要的特性就是线程cache，对于非huge的内存都是尽量从线程自身的cache中去获得，一般情况下是不需要加锁的（内存不够的时候要去中央堆申请时，就需要加锁了）。显而易见的就是，在多线程下，它的表现肯定比ptmalloc，但是根据原理来说，它的速度在分配特大内存的时候，肯定也是和ptmalloc差不多的，因为大家都是找系统要。至于大内存的归还，tcmalloc文档中提到一句：tcmalloc将逐渐的释放长时间未使用的内存给内核。完全太笼统了。后来在我们的项目中测试，它持有的内存和ptmalloc差不多，表现也不是太好，因为有一些参数需要调优而我们并没有了解到。
一开始我始终不能理解span和其他索引的关系，后来反复的思考才有感觉了。其实它和ptmalloc里面的chunk有点像，span被拿到之后是会被分割成大对象或者小对象的，这些对象会落入到前面提到的各个级别的管理索引中去，同时呢，span list还要用来管理这些span。
