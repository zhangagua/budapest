title: non volatile memory与数据库
date: 2016-04-19 11:18:02
tags: 数据库
categories: 数据库
---

non volatile memory（nvm），非易失性内存。wiki上的解释说，非易失性内存包含了read-only memory, flash memory，还有类似磁盘、以及更早的纸带。然后后来听说了一种叫做NVDIMM的硬件，使用混合易失内存+非易失内存硬件，通过SSD上常见的超级电容来维持掉电后瞬时的数据转移电力（从易失硬件到非易失硬件），保证数据的可靠性。原理是不神奇的，但是效果是惊艳的。有一天你的台式机突然断电了，然后在等待了几分钟之后供电又恢复了，此刻你的电脑屏幕亮起，整个系统和断电之前几乎一模一样，完全感觉不到断电对电脑的影响，你甚至正好用这个时间去泡了个茶。这个中文有一个词语用来描述它，全系统保护，不过英文中没有找到对应的概念。在一些国外的论文中，直接使用nvm或者non volatile memory，指代的就是类似NVDIMM这种内存（后文中的nvm也是这种指代）。

NVDIMM的速度慢于dram，快于ssd，在速度上填补了两者之间的鸿沟。它的特点是持久（非易失），随机读写。
对于以磁盘为主的数据库来说，可以假设把磁盘完整的替换成nvm（先不考虑成本）；
而对于内存数据库来说，可以以整个nvm做为主存或者使用dram+nvm的混合架构，提高自身的可靠性。
最后发现两者趋同了，都在往dram+nvm混合方向走。
这两天读了一篇文章：[A Prolegomenon on OLTP Database Systems for Non-Volatile Memory](hstore.cs.brown.edu/papers/hstore-nvm.pdf)，来自布朗大学做hstore实验室的文章，在架构上做了一些初探。

以Hstore和Mysql（innodb为引擎）为两个类型的数据库代表。架构上使用纯nvm和混合dram+nvm两种作为代表。还有两种工具，一个是NUMA node（非同一内存节点，粗糙的理解为一台计算的内存是分布式的，可以散落在多个节点上），另一个是PMFS（Persistent Memory File System）。配置bios，将每个cpu辖内存中的一半模拟成一个NUMA node，通过写cpu寄存器将这个node的访问速度降低来模拟成nvm（nvm区）。为啥不直接用nvm呢。一开始我以为它要么没钱，要么有钱也不愿意买。后面才知道其实nvm和dram物理上上没有区别。然后又将nvm区化为两个小分区，一个是NUMA node，一个是pmfs。

 - Hstore，全nvm架构，nvm作为主存，利用pmfs将另一半nvm作为日志系统
 - mysql，全nvm架构，nvm作为为主存，利用pmfs将另一半nvm作为主存储器
 - Hstore，混合架构，dram作为主存，利用pmfs将另一半nvm作为日志系统以及换出（anti-cache，主存不足时，使用类似内存swap的技术，将部分冷内存换出到nvm）
 - mysql，混合架构，dram作为主存，利用pmfs将另一半nvm作为主存储器
 
下面两张图很清晰的说明了上面的四个类型

![架构图1](/images/all_nvm.png)
![架构图2](/images/h_nvm_dram.png)

不多说了，直接测试结果的图
![测试结果图](/images/nvm_test_result.png)

图中有个东西要解释下：workload skew。原文中是这样的解释的：
>In addition to the read-write mix, we also control the amount of skew that determines how often a tuple is accessed by transactions.

也就是说倾斜度（skew）决定的是一行数据（tuple）被访问的频率，倾斜度越高，证明访问数据重复度越高，反之，访问的越分散。

最让人疑惑的是在高重复度访问的时候，为什么mysql的表现这么差。照理说mysql也是有缓存的，不应该这么差。我觉得访问循环是这样的
```
for(i = 0; i < 10000000;i++)
{
    访问A表的x行
    访问B表的y行
    ...
    访问Z表的z行
}
```
这样的访问方式，可能导致mysql的缓存命中率特别低。不然的话，我觉得完全在内存中的mysql不会这么差劲吧。

在低重复度的情况下，重写入负载的情况下， mysql的性能其实和hstore还是比较接近的了。具体的微小的差异可能和mysql中写入时为了可靠性采用的策略有关系。比如mysql采用的double write，在这种随机写入性能很好的nvm上其实是完全不需要的。hstore毕竟是一个实验性质的OLTP内存数据库，具体到真正商用的内存数据库，我觉得性能上可能要比它好一些。

总的来说，最具有有诱惑力的是一开始提到的全系统保护，写入内存即可靠。nvm最终来说对磁盘数据库的冲击不大，但是对于内存数据库来说，确实非常关键的一环。
