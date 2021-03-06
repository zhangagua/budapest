---
title: 数据库里面的JOIN实现
date: 2016-08-13 00:06:14
tags: 数据库
categories: 数据库
---

数据库里面通常有三种join的实现方式：

* hash join
* nested loop hash
* sort merge hash

这些属于是基础知识，我就算是我自己复习吧。

# 前提

假设有两张表A与B。其中A是一张大表，B是一张小表。在A.a和B.b两个列做连接。

## hash join

hash join仅仅能处理等值join。假设我们是对A、B两个表进行等值Join。
我们称B表为build table，因为我们先用B构建hash table，接着，我们用A中的行来一点一点探测这个hash table，在探测过程中既可获得等值连接的结果。
存放哈希表的时候，如果内存足够，那么就是optimal_hash_join，如果内存不够，那么还需要分区。

### optimal hash join

先上一张图，然后我用c++伪码来解释。

![optimal_hash_join](/images/optimal_hash_join.png)

这张图好多博客都有，应该源自一篇oracle的论文还是官方的资料，遗憾的是我并没有找到。
__其核心就是通过hash函数，在两个列中把hash key一样的部分找出来，然后按照条件拼到一起。__
为了方便理解，我用c++伪码来实现一下这个过程，意会一下。

```c++
void hash_join(table A, table B, vector<pair<type, type> >& result)
{
    unordered_map< type, vector<type> > hash_table;
	//构建哈希表
    for(auto item_b : B.b)
    {
        auto ite = hash_table.find(item_b);
        if(ite != hash_table.end())
        {
            (ite->second).push_back(item_b);
        }
        else
            hash_table.insert({item_b, item_b});
    }
    //通过构建好的哈希表来做等值join
    for(auto item_a : A.a)
    {
        auto ite = hash_table.find(item);
        if(ite != hash_table.end())
        {
            for(auto item_b : (ite->second))
            {
                result.push_back({item_a, item_b});
            }
        }
    }
}
```

### 先分区再hash

在上一节中，假设用B构造的hash table太大，内存装不下，怎么办？
首先我们选用一个hash函数，通过对A.a和B.b进行哈希，将A和B表都分开。分区以后：
A = {A1,A2,A3,A4}
B = {B1,B2,B3,B4}
假设这个函数是对3进行取模，那么A表中A.a/3余数0的都在A1表，B表中B.b/3余数0的都在B1表，然后再对A1表和B1这两个子表进行optimal hash join产生result1。
以此类推，result1、result2等等合并起来，就是最终结果了。
能这样成功获得结果的前提是：B的子表构造的hash table内存能装的下。
如果还是装不下呢？

思路1：再切，A11，A12，A13，B11，B12，B13。然后result1 = result11，result12，result13；
思路2：切B11，B12，B13，但是A1不切了，拿整个A1去分别去探测B11，B12，B13。

显然第一个思路是好的，但是不幸的是就网上的资料显示，oracle可能采用的是第二种思路。第二种处理方法还有个名字，叫nest loop hash join或者是multipass hash join。

## nest loop join

最原始的join方法，把A表中的每一行拿到手，在B表中去循环探查，获得结果。
用伪码描述等值join，一下就懂：

```c++
for(auto item_a : A.a)
{
    for(auto item_b : B.b)
    {
        if(item_a == item_b)
            result.push_back({item_a,item_b});
    }
}
```

简单吧？外层循环的叫outer table，里面的叫inner table。
如果A.a或者B.b都没有索引，那么总共循环次数就是M*N（M是A表行数，N是B表行数）。谁是inner谁是outer没有关系。
但是如果inner table有索引，那么问题就发生了变化了。有索引查找的时间复杂度问为logN哦。于是总循环次数就是M*logN哦，所以inner table最好是要有索引哦！

```c++
for(auto item_a : A.a)
{
    vecter look_rt = look(auto item_a in B.b);
    for(auto item : look_rt)
    {
        result.push_back({item_a,item});
    }
}
```

## sort merge join

能使用这种方式的前提是，两个做连接的列是排序好的。一般来说数据是无序的时候，得要先排序，像我们的中间数据是天然排序排好的，所以省略了第一个步骤。
也是以等值join做例子来说

![sort_join.jpg](/images/sort_join.jpg)

两个偏移量，核心伪码拷贝过来。

```c++
if(A.a[i] == B.b[j])
    result.push_back({A.a[i], B.b[j]});
else if(A.a[i] > B.b[j])
    j++;
else if(A.a[i] < B.b[j])
    i++;
```

时间复杂度是O(M+N);

