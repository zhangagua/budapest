title: 针对uint64的哈希存储调研和优化
date: 2016-04-23 05:22:16
tags: 程序猿
categories: 杂项
---

# 背景

是这样的，在实际的项目中我们遇到一个内存爆炸的问题。具体的场景是，我们需要存储一个key=》value以及他们的映射关系。这个key的特征是基本均匀的分布，比如3、104、233、310...，不重复而且散的比较均匀，value的特征也差不太多，强调一点就是key和value没有逻辑关系，而且key基本上是不可能等于value的（/(ㄒoㄒ)/~~，后面就是因为测试的时候没有注意到这个点，以为自己找到了一个解决了问题的哈希，好在最后读了源码发现我正好落在了优化的坑里，要不然直接替换进项目里就粗大事了）。当我们插入千万级别的键值对之后，我们发现基本上是原始数据的4倍到5倍的样子。比如插入1KW的键值对，内存占用是394M多~原始数据本身（key+value）大概也就是150M的样子。这种两倍膨胀在我们系统中比较普遍的存在，内存这样被浪费掉了，而内存的消耗情况是我们系统的一个重要评测指标。

# 各路哈希
找源码了解其实现，才能够解决这个问题。unordered_map里面使用的是bits/hashtable.h中的哈希表,和以前侯捷老师写的那个stl源码解析不太一样（不知道新版本的是不是更新了，我自己也没有看过这本书。因为这个关系，直接看unordered_map源码有点头大，因为对模板元的路数一无所知）。先扫一眼unordered_map的源码。牢记关注点是存储结构。

```
/*代码1*/
class unordered_map
{
    typedef __umap_hashtable<_Key, _Tp, _Hash, _Pred, _Alloc>  _Hashtable;
    _Hashtable _M_h;
    public:
        typedef typename _Hashtable::key_type        key_type;
        typedef typename _Hashtable::value_type      value_type;
        typedef typename _Hashtable::mapped_type     mapped_type;
        typedef typename _Hashtable::hasher          hasher;
        typedef typename _Hashtable::key_equal       key_equal;
        typedef typename _Hashtable::allocator_type  allocator_type;
        ......
}
```
[unordered_map源码地址](https://gcc.gnu.org/onlinedocs/libstdc++/latest-doxygen/a01577_source.html)
整个unordered_map里面其实就是一个hashtable，而后确认是这个hashtable([bits/hashtable.h源码地址](https://gcc.gnu.org/onlinedocs/libstdc++/latest-doxygen/a01228_source.html))。继续进来看看。
```
/*代码2*/
template<typename _Key, typename _Value, typename _Alloc,
     typename _ExtractKey, typename _Equal,
     typename _H1, typename _H2, typename _Hash,
     typename _RehashPolicy, typename _Traits>
class _Hashtable
{
    ... ...
    using __bucket_type = typename __hashtable_alloc::__bucket_type;
    ... ...
	private:
		__bucket_type*        _M_buckets;
		size_type         _M_bucket_count;
		__node_base       _M_before_begin;
		size_type         _M_element_count;
		_RehashPolicy     _M_rehash_policy;
	... ...
}
```
显然关注点在\_M_buckets。
\__bucket_type在\__hashtable_alloc中定义在std::__detail::_Hash_node_base Definition:hashtable_policy.h:230。继续进去看，落到hashtable_policy.h（[源码地址](https://gcc.gnu.org/onlinedocs/libstdc++/latest-doxygen/a01230_source.html)）上。
```
/*代码3*/
 /**
 *  struct _Hash_node_base
 *
 *  Nodes, used to wrap elements stored in the hash table.  A policy
 *  template parameter of class template _Hashtable controls whether
 *  nodes also store a hash code. In some cases (e.g. strings) this
 *  may be a performance win.
 */
struct _Hash_node_base
{
  _Hash_node_base* _M_nxt;
  _Hash_node_base() noexcept : _M_nxt() { }
  _Hash_node_base(_Hash_node_base* __next) noexcept : _M_nxt(__next) { }
};

/**
 *  struct _Hash_node_value_base
 *
 *  Node type with the value to store.
 */
template<typename _Value>
struct _Hash_node_value_base : _Hash_node_base
{
  typedef _Value value_type;
  __gnu_cxx::__aligned_buffer<_Value> _M_storage;
  ... ...
};

...
    using __node_base = __detail::_Hash_node_base； /*关键行1*/
    using __bucket_type = __node_base*; /*关键行2*/
...
```

好的，这就是我们的最基本的node,真正的数据结构就两个：一个指针 _M_next, 一个_M_storage。__aligned_buffer只是起到对齐的作用。ok,好的，node的结构知道了，现在需要找出node的组织方式。在代码3三的后半部分你会发现这两行代码，结合代码2，我们可以大致知道hashtable的内存布局了。比对着代码在推敲一下，画了如下的一个图。
![hashtable内存结构图](/images/hashtable_mem_ac.jpg)
使用的是拉链处理冲突的方式。于是我开始纠结了，Key存储在哪儿呢？_Value是放在_M_storage,可是我们每次查找的时候肯定也要对比key吧？不然同一个哈希bucket里面，我们没法确定谁才是我们要得value啊？！key必须要和hash出来的code产生关系，而且要和value有直接映射。于是我睁大了眼睛找了足足两个小时，愣是没找到这儿key去了哪儿了（对模板元和stl的编码方式不熟悉，注释还少，粗略理解没有问题，但是精确起来就说不太清楚了。(⊙﹏⊙)b）。
由于时间有限，我觉得我不能采用这种一直推敲源码的方式，我觉得必须得换种方式。而后我跑去cplusplus看看unordered_map文档，看看会不会有意外收获。果然是有啊！
![unordered_map cplus图](/images/unordered_map_cplus.png)

红色的部分注意到了吧？
再看unodered_map的源码里面有这么一句
>typedef typename _Hashtable::value_type   value_type;

再看hashtable里面
>typedef _Value                        value_type;

所以，其实一个_M_storage里面存储的就是一个pair<key,value>。_Value既有key也有value！
我们来算算，按照1000w个pair<uint64_t, uint64_t>，极限情况下（即每一个哈希槽里面只有一个元素，没有任何冲突），一个pair还得带两个指针（一个是_M_buckets中指向这个pair的指针，一个是这个pair指向下一个pair），在64位机器上一个指针是8Byte，于是极限情况下，一个pair真实的存储开销是8Bytes * 4 = 32Bytes。按照1000w个来粗算，大概是320M左右。实际我们测试中发现大概是394M左右。这种情况不好说，有可能是malloc的动态库持有了部分内存，也有可能是我们落下了一个指针。
实际上我们特别希望能够减少这样的开销，如果能将4*8Bytes变成3*Bytes也是一个巨大的进步，因为实际中这样的优化出现在多台节点上，按照50亿条数据，可以省下40G数据！但是这是有前提的，就是我们的查询性能不能有大幅下降，省内存的条件必须是保证性能的前提下。初步估计，采用拉单链的方式来处理冲突，应该可以获得内存上的预计优势，但是不太确定在查询时的性能。
除了自己实现以外，我还去找了其他的一些库里面的函数来做对比测试 。有的哈希表现的比较有特点，感兴趣的我还特意去看了相关源码。最后发现都不是太满意，存储性能上去了查询性能必然下降，最终还是要取舍。

先列出我们测试的对象。

>glibc-2.17 unordered_map
>google sparsehash
>google densehash
>boost unoredered_map
>自己实现的vector+unordered_map 组合
>自己实现的vector+vector 组合
>glib-2.47 ghash_table
>bullet3中的哈希(各种奇怪的！不择手段的试！)

简单的介绍一下上面的各个哈希。第一个已经是std名空间里了，综合性能相当不错了。第二个第三个都是几年前googlecode上面的sparsehash和densehash（现在是在github上），至于到底是不是google的没有深究过，从名字来看也知道，一个是针对稀疏的key优化的，一个是针对密集key的优化。sparse还着重于存储优化，牺牲了速度；dense着重于速度，以大内存消耗为代价。第四个就是boost名空间中的unordered_map，和std里面的哈希实现还不太一样，不清楚是否会有很好的表现。自己实现的vector+unordered_map，简单粗暴，hash直接取模，落到vector上直接存储，冲突或者是落到vector的size外面时，直接插入一个统一的unordered_map。自己实现的vector+vector就是在冲突和落到外面的时候直接插入该槽指向的vector。glib-2.47的哈希来自于gnome。（下面用umap代表unordered_map）

|变量|vector+umap|vector+vector|sparsehash|densehash|umap|glib|
|:-------:|:-------:|:-------:|:-------:|:-------:|:-------:|:-------:|
|LOOP_SIZE|1亿|1亿|1亿|1亿|1亿|1亿|
|MEM_SIZE(KB)|1986200|1576048|1640088|4195640|3918804|1174680/19562965456|
|STEP_SIZE|1|1|1|1|1|1|
|构建时间|2935|5404|7942|2973|10971|6541|
|查询时间|1872|3847|3748|496|2596|2970|
|优化选项|O2|O2|O2|O2|O2|O2|

|变量|vector+umap|vector+vector|sparsehash|densehash|umap|glib|
|:-------:|:-------:|:-------:|:-------:|:-------:|:-------:|:-------:|
|LOOP_SIZE|1亿|1亿|1亿|1亿|1亿|1亿|
|MEM_SIZE(KB)|162252|202800|99852|4195480|208932|99832/165456|
|STEP_SIZE|19|19|19|19|19|19|
|构建时间|540|889|1183|1794|598|618|
|查询时间|214|147|457|99|159|259|
|优化选项|O2|O2|O2|O2|O2|O2|

第一个表格里面表示的是插入1亿条数据，第二个是从0~1亿循环，每19个数插入一次。
glib的内存（MEM_SIZE）有两个。第一次测试的时候，我采用的是(i,i)这样的方式，也就是key和value是一样的键值对。
当时结果出来之后出人意料的好，心中觉得特别的疑惑，于是去看了源码。发现一个蛋疼的问题...初始化的时候，hash的成员变量keys和values指向同一块内存区域，插入的时候，如果发现value不同于key的时候，再开辟一块内存区域，再让values指向过去。由于我测试的时候key和value都是相同的，所以最后keys和values只存了一份儿。(-__-)b。差点就玩脱了，还好还好。后面一个MEM_SIZE就是一个比较正常的数值了，没有非常惊艳的表现。
从表格中看出来，觉得最适合的可能是vector+umap这种类型的哈希。不过整个身家性命（MEM_SIZE）交给两个库容器，心里十分的不踏实，第一次哈希取模直接落到vector上，处理冲突的方式采用的是umap，理论上存在速度和内存开销都比纯umap差的情况，不过个人觉得这种情况可能是少见的，这个和我们的数据特性相关。不过就算是这样，也非常的犹豫到底要不要放到项目中去，尤其是测试了小批量数据的时候，查询速度稍慢，内存性能稍优。如果采用最简单的方式，就是vector+list的形式，采用单链表来处理冲突，查询性能和内存开销肯定都是差于vector+vector的方式的,尽管构建速度肯定是前者占优。所以这里我们并没有考虑这种方式。

分享一篇讲google_sparse_hash实现的文章([google_spare](http://smerity.com/articles/2015/google_sparsehash.html))
这篇质量稍差[hash_test](http://preshing.com/20110603/hash-table-performance-tests/)，开放地址一般不会比dense哈希快吧？


