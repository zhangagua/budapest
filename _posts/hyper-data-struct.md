---
title: 基于机器码的数据库HyPer（二）（存储结构篇）
date: 2016-06-21 11:21:45
tags: 数据库
categories: 数据库
---

前一篇的重点放在了科普黑科技HyPer怎么样结合c++，LLVM，JIM compiler，而这一篇的重点将放在HyPer的存储结构上。
HyPer是全内存的数据库，混合支持OLAP和OLTP，上一篇在中我们已经看到了，它在OLAP的性能测试中有着惊艳的表现，大大超出了纯c++实现。首要的功臣自然是他们基于机器码的查询引擎实现，不过这样的查询引擎在实现的难度也应该有点令人生畏惧。在数据库中，和查询引擎紧密结合的存储结构，也是非常重要的一环，它将不仅直接影响存储开销（珍贵的内存），同时存储结构也会直接决定数据的操作的方法。官方首页中提到了两篇基础论文，其中一篇说明了HyPer拥有multi-version concurrency control，并且提到了关于mvcc方方面面的策略，但是并没有详细的介绍存储结构。而在2016年发的一篇paper，[Data Blocks: Hybrid OLTP and OLAP on Compressed Storage using both Vectorization and Compilation](http://db.in.tum.de/downloads/publications/datablocks.pdf)，才算是比较细致的讲解它们内存的存储结构，压缩方式，索引结构。Hyper这么多篇工程论文，工程类顶会的分量还是很重的，真的不得不说它里面的点还是很多的。
论文基本上是看完了，现在就是结合论文中的图以及部分原文，来说明HyPer现在的内存数据结构等。

![hyper_datablock_ac](/images/hyper-datablock-ac.png)

内存中的数据分为热数据和冷数据，热数据采用不压缩的方式来存放，而冷数据采用datablock的手段来存储，每个datablock内部可能存在多种压缩方式。冷热数据的区分是按照被插入删除修改操作的频繁程度来决定，对于查询操作来说，都可以快速的去查询，不过鉴于结构不一样，查询时驱动的模块肯定也是不一样的。

![hyper-datablock-compressed-overview](/images/hyper-datablock-compressed-overview.png)

这里(a)中的chunk打包之后也就是一个datablock了，我们看到一个chunk是横切的，每个datablock里面存储了一行所有的属性，而存储时对每一列是采用了不同的压缩算法，主要包括三种：字典压缩，truncated，single value这三种压缩方式。有意思的时采用字典压缩时，字典和数据可以不是连续的。
这里(b)中介绍的是他们使用一个轻量级索引，主要用来加速scan的，Positional SMAs。看之前先了解一下全称为Small Materialized Aggregates的SMA。关于SMA的paper在这里[Small Materialized Aggregates: A Light Weight Index Structure for Data Warehousing](http://www.vldb.org/conf/1998/p476.pdf)。简单而不准确的说，就是将数据分区，记录每个分区的最大最小值，scan的时候先看该分区的范围，如果查询的条件包含了该分区的范围，那么就scan这个分区；否则，跳过该分区。PSMA则是SMA的变种。

>PSMAs are light-weight indexes that narrow the scan range within a block even if the block cannot be skipped based on materialized min and max values

这是PSMA的解释。后面有图会详细的介绍这个PSMA，我个人觉得也是用处不太大的东西，主要在数据密集集中于min这一边的时候，会有比较好的效果。我们看到在蓝色属性列和黄色属性列上有条件是，可以缩小SARGable的范围。SARGable的全称是Search ARGument ABLE，wiki解释这个词条在[这里](https://en.wikipedia.org/wiki/Sargable)。大致意思是sargable的区分标准是能否利用索引的优势。Sargable operators: =, >, <, >=, <=, BETWEEN, LIKE, IS [NOT] NULL, EXISTS。应该也是很好理解的哈。

![hyper-layout-of-datablock](/images/hyper-layout-of-datablock.png)

上面这张图示一个datablock的具体值分布图，把每一个名词都解释。tuple count代表这个datablock里面存储了多少个tuple，sma offset即sma信息的偏移量，dict offset字典偏移量，data offset压缩数据偏移量，compression这个属性压缩的方式，string offset这里不清楚为什么还会有个string，怪怪的。特别说明一下，我感觉sma offset0指向的地方其实就是min0开头这个位置。这个东西理解起来也没有什么难度哈。然后我们接往下看，这是关于这个轻量索引psma的东西。看下面的图时候一定要结合上面一张图来理解。

![hyper-psmas](/images/hyper-psmas.png)

sma min 和max 就是上面datablock layout中的min和max了。重点解释一下lookuptable就可以了。
我这样来解释，lookuptable中的每一个元素是一个data偏移量范围，代表一个值或者一个范围的值出现的区间。我们以偏移量为0的这个元素来解释。[1,17) 1，代表2这个数字出现在data区偏移量区间为[1,17)，既表示你要扫描那就仅仅扫描这个范围即可，于是我为了方便理解，我就记为 2->[1,17)。
图中还举了个例子，那就是在探测998。图中给了计算过程我就不罗嗦了，我强调的是其实lookuptable这个表的第259元素不仅仅是998->[6,7)，而是[0x2+0x0300,0x2+0x03FF)->[6,7）。换成十进制就是[2+768,2+1023)->[6,7）。lookuptable最右边的值（1，2^8, 2^16）。越到表后面，值范围越大。换而言之，越靠近min，那么通过lookup定位效果越好。为什么我不是说越精确呢，因为可能出现这种情况2->[0,data offset end)，也就是当一个值靠近min，然后它出现在了data中offset为0的位置和data中最后一个位置，而且这个值仅仅只出现了这两次，但是为了还是需要扫描整个data区域，所以我们不能使用精确来形容这个东西。


# 个人感受

我个人感觉从数据结构和之前的原理篇的论文来说，Hyper给人的感觉都是一致：做到极致。
如果仅仅是单独读了数据结构这一篇论文的话，我肯定会认为这个数据库有点花哨（换句话说就是太复杂了），而且不一定实用。但是结合到前面的一篇文论来看，它不是想要花哨，而是想要在每一个方面都做到极致。
但是我还是有点持怀疑的态度，毕竟我并没有发现有它被投入到了大规模的生产环境中去应用。
所以，更应该以科研的眼光来看待Hyper。
