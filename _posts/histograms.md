title: Oracle的直方图
date: 2017-12-08 11:18:02
tags: 数据库
categories: 数据库 SQL
---
# 直方图
直方图是一个特定列的统计数据，它能够提供关于该列中值的分布信息。直方图的数据排序后放于buckets中，正像你会将硬币排好序放入buckets中一样。
基于NDV(Number of distinct values. The NDV is important in generating cardinality estimates.)和数据的分布，数据库会自动选择建立何种直方图。直方图的类型包括：
1. 频率直方图和top频率直方图（Frequency histograms and top frequency histograms）
2. 高度平衡直方图（Height－Balanced histograms）
3. 混合直方图（Hybrid histograms）

下面的章节按序讨论一下内容：
1. 直方图的目的
2. Oracle合适建立直方图
3. Oracle怎么选择直方图的类型
4. 使用直方图来获得Cardinality的算法
5. 频率直方图
6. Top频率直方图
7. 高度平衡直方图
8. 混合直方图

## 直方图的目的
优化器默认不同值的分布是均匀的（uniform distribution）。但是实际上，一个列会存在值倾斜（data skew，a nonuniform distribution of data within the column），当filter或者join谓词涉及的列拥有直方图时，它能够让优化器有能力更加精确的估计cardinality。
比如，一个在California的书店百分之95%的书籍销售到California，4%到Oregon，1%到Nevada。存储这些订单的表拥有30000行，其中一个列存储了该本书将被销售到何处。假设此时有一个query查询有多少本书籍将被销售到Oregon。当没有直方图的时候，优化器假设均匀分布，该列的NDV等于3，因此将预估cardinality等于30000/3=10000行。根据这种估计，优化器最终回选择一次full table scan。有了直方图，优化器可以计算出大约4%的数据将被销售至Oregon，并且会选择一次index scan。

## Oracle何时会选择建立直方图
如果DBMS_STATS收集table的统计数据，并且查询引用了该table的部分列，那么Oracle会根据之前的查询负载自动的构建直方图。
基本的过程如下：
1. 为该table运行DBMS_STATS（其中METHOD_OPT参数默认被设置为SIZE AUTO）
2. 一个用户的查询了该表
3. Oracle观察到查询中的谓词，更新data dictionary table SYS.COL_USAGE$。
4. 你再次运行DBMS_STATS，它会去查询SYS.COL_USAGE$并根据之前的查询来决定在哪些列上需要建立直方图。
AUTO的特性还将包含一下步骤：
* 随着多次查询的发生，DBMS_STATS可能会改变自己要收集的统计信息。例如，尽管表中的数据没有变化，查询和DBMS_STATS可能导致对相应表的计划发生变化。
* 如果你为一张表收集了统计信息，但是没有去查询这张表，Oracle不会为表中的列去构建直方图。为了让数据库自动构建直方图，你必须要查询一次或者多次列，使得列被使用的信息被纪录在SYS.COL_USAGE$中。

例子11-1自动构建直方图
假设sh.sh_ext是一个包含和sh.sales一样数据的表。你新建一个sales2表，并且使用sh_ext进行bulk load（它将会自动为sales2创建统计信息），你也可以创建如下的索引
```SQL
SQL> CREATE TABLE sales2 AS SELECT * FROM sh_ext;
SQL> CREATE INDEX sh_12c_idx1 ON sales2(prod_id);
SQL> CREATE INDEX sh_12c_idx2 ON sales2(cust_id,time_id);
```
你先查询一下数据字典，看看sales2是否拥有已经建立的直方图。因为sales2还没有被查询过，所以它是不会有直方图的。
```SQL
SQL> SELECT COLUMN_NAME, NOTES, HISTOGRAM 
  2  FROM   USER_TAB_COL_STATISTICS 
  3  WHERE  TABLE_NAME = 'SALES2';

COLUMN_NAME   NOTES          HISTOGRAM
------------- -------------- ---------
AMOUNT_SOLD   STATS_ON_LOAD  NONE
QUANTITY_SOLD STATS_ON_LOAD  NONE
PROMO_ID      STATS_ON_LOAD  NONE
CHANNEL_ID    STATS_ON_LOAD  NONE
TIME_ID       STATS_ON_LOAD  NONE
CUST_ID       STATS_ON_LOAD  NONE
PROD_ID       STATS_ON_LOAD  NONE
```
然后你查询一次sales2，条件为product id＝42，然后使用GATHER AUTO来收集表的统计信息。
```SQL
SQL> SELECT COUNT(*) FROM sales2 WHERE prod_id = 42;

  COUNT(*)
----------
     12116

SQL> EXEC DBMS_STATS.GATHER_TABLE_STATS(USER,'SALES2',OPTIONS=>'GATHER AUTO');
```
再一次查询data dictionary你会发现Oracle已经为prod_id这个列创建了一个直方图。
```SQL
SQL> SELECT COLUMN_NAME, NOTES, HISTOGRAM 
  2  FROM   USER_TAB_COL_STATISTICS 
  3  WHERE  TABLE_NAME = 'SALES2';

COLUMN_NAME   NOTES          HISTOGRAM
------------- -------------- ---------
AMOUNT_SOLD   STATS_ON_LOAD  NONE
QUANTITY_SOLD STATS_ON_LOAD  NONE
PROMO_ID      STATS_ON_LOAD  NONE
CHANNEL_ID    STATS_ON_LOAD  NONE
TIME_ID       STATS_ON_LOAD  NONE
CUST_ID       STATS_ON_LOAD  NONE
PROD_ID       HISTOGRAM_ONLY FREQUENCY
```
# Oracle是如何选择直方图的类型的？
Oracle数据库使用多种多种策略来决定何种直方图将被创建：频率直方图，Top频率直方图，高度平衡直方图，混合直方图。
影响决定的值有三个：
NDV：也就是一个列中不同值的个数
n：代表直方图的buckets数，默认为254
p：是一个值，计算方法为(1–(1/n)) * 100。假设n＝254，那么p就是99.6.
另外一个条件就是DBMS_STATS中的estimate_percent被设置成了AUTO_SAMPLE_SIZE.
下面的图显示了构造直方图的决策树。
![Histogram](/images/histogram/cho.png)

# 使用直方图的时候如何计算Cardinality
使用直方图，计算Cardinality依赖于endpoint numbers and endpoint values，并且和column values是不是popular或者nonpopular。
下面的章节将介绍：
1. endpoint numbers and endpoint values
2. popular and nonpopular
3. bucket压缩

## endpoint numbers and values
endpoint number能够唯一的标示一个bucket。在频率和混合直方图中，一个endpoint number表示在此bucket之前所有值的个数。比如说，一个bucket的endpoint number是100，那么意味着在包含此bucket在内的，之前所有的bucket中的一共有100个值。在高度平衡直方图中，优化器按照0到1开始为endpoint number进行赋值。在所有的情况下，endpoint number和bucket number是对等的。

endpoint value表示在该bucket中存在的最大值。例如，一个bucket包含有52794和52795，那么他的endpoint value则是52795.

## Popular and Nonpopular values
Popular value会影响优化器对cardinality的估计，具体如下：
* Popular values：当两个或者多个bucket含有同一个endpoint value时，popular value就出现了。优化器决定一个值是否是是popular value首先会检查它是否是一个endpoint value。在频率直方图中，优化器还会去检查该bucket前面的一个bucket是不是该值（如果是，那么它就是一个popular value）。在混合直方图中会直接纪录bucket的endpoint value在bucket内出现的次数。如果这个value超过1，那么它就是一个popular value。
优化器通过一下的公式计算popular value的cardinality：
```
cardinality of popular value = 
  (num of rows in table) * 
  (num of endpoints spanned by this value / total num of endpoints)
```
* Nonpopular values
  如果一个值不是popular value那么它肯定就是一个nonpopular value。优化器计算这种值的cardinality方式为：
```
cardinality of nonpopular value = (num of rows in table) * density
```
优化器通过内部算法，以NDV和bucket数量为基础，计算获得density。这个值越靠近1则意味着优化器期待它更多行含有该值。
## bucket压缩
在一些情况下，为了减少bucket的总数量，我们会对bucket进行压缩，将多个压缩成1个。例如，对于频率直方图来说，看下面的分布。
```
ENDPOINT_NUMBER ENDPOINT_VALUE
--------------- --------------
              1          52792
              6          52793
              8          52794 
              9          52795
             10          52796
             12          52797
             14          52798
             23          52799
```
bucket number(endpoint number)从1到23。但是其中有一些number不见了，比如说11，比如说15，16。这些bucket其实就是被压缩了。压缩的原理很简单，当15～23的endpoint value都是52799的时候，我们就可以将这几个bucket压缩成一个bucket。
具体的：
```
ENDPOINT_NUMBER ENDPOINT_VALUE
--------------- --------------
              1          52792 -> nonpopular
              6          52793 -> buckets 2-6 compressed into 6; popular
              8          52794 -> buckets 7-8 compressed into 8; popular
              9          52795 -> nonpopular
             10          52796 -> nonpopular
             12          52797 -> buckets 11-12 compressed into 12; popular
             14          52798 -> buckets 13-14 compressed into 14; popular
             23          52799 -> buckets 15-23 compressed into 23; popular
```
# 频率直方图
在频率直方图中，每一个独立的列值都会有一个bucket。因为每个bucket都包含特定的值，那么一些bucket可能包含多一些值，一些bucket包含少一些值。一个简单的理解就是，就像我们整理硬币一样，1毛钱的放进同一个bucket可能有100个，1块钱放一个bucket可能有10个。
## 构建频率直方图的原则
就想之前提过的，构建频率直方图的条件之一是要求被构建的bucket数量。
* NDV小于或者等于n（默认为254）
* 在DBMS_STATS中的estimate_percent值被设定为AUTO_SAMPLE_SIZE或者用户指定的特殊值
##产生频率直方图
此章节将会通过简单的方案阐述频率直方图的产生。
假定你想要在sh.countries.country_subregion_id column上产生一个直方图。这个表拥有23行数据。下面的查询能够看到数据分布：
```
SELECT country_subregion_id, count(*)
FROM   sh.countries
GROUP BY country_subregion_id
ORDER BY 1;
 
COUNTRY_SUBREGION_ID   COUNT(*)
-------------------- ----------
               52792          1
               52793          5
               52794          2
               52795          1
               52796          1
               52797          2
               52798          2
               52799          9
```
最后产生的直方图如下
```
SELECT ENDPOINT_NUMBER, ENDPOINT_VALUE
FROM   USER_HISTOGRAMS
WHERE  TABLE_NAME='COUNTRIES'
AND    COLUMN_NAME='COUNTRY_SUBREGION_ID';
 
ENDPOINT_NUMBER ENDPOINT_VALUE
--------------- --------------
              1          52792
              6          52793
              8          52794
              9          52795
             10          52796
             12          52797
             14          52798
             23          52799
```
直方图的图形化数据展示如下：
![Frequency Histogram](/images/histogram/GUID-385205A6-6198-4143-9D10-106AC730C9AE-default.png)
假设我们想要计算52799的cardinality（C）的值，
C = 23 * ( 9 / 23 )
1，9，10号bucket包含有nonpopular值，优化器将会通过density来计算他们的cardinality。

# Top频率直方图
Top频率直方图是频率直方图的一个变种，它最大的特殊是它将忽略所有的nonpopular value。例如有1000个硬币，这里面仅仅只有1个1毛钱的其他都是1块钱的，那么我们将硬币放入bucket的时候就忽略这个1毛钱，都放到一个bucket里面就好了。
## 构建Top频率直方图的原则
* NDV大于n
* Top频率直方图中涉及的值数量必须超过概率p（p＝p is (1-(1/n))*100，n为254时，p为 99.6）
* DBMS_STATS中的estimate_percent被设置成了AUTO_SAMPLE_SIZE
## 构建Top频率直方图
为sh.countries.country_subregion_id的列构建top frequency histogram。假设分布为：
```
SELECT country_subregion_id, count(*)
FROM   sh.countries
GROUP BY country_subregion_id
ORDER BY 1;
 
COUNTRY_SUBREGION_ID   COUNT(*)
-------------------- ----------
               52792          1
               52793          5
               52794          2
               52795          1
               52796          1
               52797          2
               52798          2
               52799          9
```
假设我们指定要建立一个7个bucket的直方图，由于该列含有8个不同的值，而n＝7，此时系统只能为它建立一个top频率直方图或者是混合直方图。由于Top 7的值占必有22行，比例为95.6%，超过了此时的p值（85.7）。所以构建一个Top频率直方图。
```
SELECT ENDPOINT_NUMBER, ENDPOINT_VALUE
FROM   USER_HISTOGRAMS
WHERE  TABLE_NAME='COUNTRIES'
AND    COLUMN_NAME='COUNTRY_SUBREGION_ID';
 
ENDPOINT_NUMBER ENDPOINT_VALUE
--------------- --------------
              1          52792
              6          52793
              8          52794
              9          52796
             11          52797
             13          52798
             22          52799
```
Top频率直方图的图形化数据展示如下：
![Top Frequency Histogram](/images/histogram/GUID-DB85F6DA-EC41-4C6C-93F4-7032047AD19E-default.png)
除了52795，所有的值都拥有一个自己的bucket。因为52795是nonpopular，同时它又是statistically insignificant（不知道怎么翻译和理解，统计学上不重要的？)
（这里遗漏了对cardinality的计算，其实很简单。例如对52799进行计算，C＝22-13/22）
# 高度平衡直方图
## 构建高度平衡直方图的原则
* NDV大于N
* DBMS_STATS中的estimate_percent被设置成了AUTO_SAMPLE_SIZE
需要注意的是12c中，这种情况下构建的是Top频率直方图或者混合直方图。
##构建高度平衡直方图
假设数据分布如下，要求按照7个bucket进行分布：
```
SELECT country_subregion_id, count(*)
FROM   sh.countries
GROUP BY country_subregion_id
ORDER BY 1;
 
COUNTRY_SUBREGION_ID   COUNT(*)
-------------------- ----------
               52792          1
               52793          5
               52794          2
               52795          1
               52796          1
               52797          2
               52798          2
               52799          9
```
产生直方图如下
```
SELECT COUNT(country_subregion_id) AS NUM_OF_ROWS, country_subregion_id 
FROM   countries 
GROUP BY country_subregion_id 
ORDER BY 2;
 
NUM_OF_ROWS COUNTRY_SUBREGION_ID
----------- --------------------
          1                52792
          5                52793
          2                52794
          1                52795
          1                52796
          2                52797
          2                52798
          9                52799
```
高度平衡直方图的图形化数据展示如下：
![Height-Balanced Histogram](/images/histogram/GUID-96BEFED9-3A90-4E24-B475-6008AFCDE73F-default.png)
The optimizer must evenly distribute 23 rows into the 7 specified histogram buckets, so each bucket contains approximately 3 rows. However, the optimizer compresses buckets with the same endpoint. So, instead of bucket 1 containing 2 instances of value 52793, and bucket 2 containing 3 instances of value 52793, the optimizer puts all 5 instances of value 52793 into bucket 2. Similarly, instead of having buckets 5, 6, and 7 contain 3 values each, with the endpoint of each bucket as 52799, the optimizer puts all 9 instances of value 52799 into bucket 7.

In this example, buckets 3 and 4 contain nonpopular values because the difference between the current endpoint number and previous endpoint number is 1. The optimizer calculates cardinality for these values based on density. The remaining buckets contain popular values. The optimizer calculates cardinality for these values based on endpoint numbers.

# 混合直方图
混合直方图混合了高度平衡直方图和频率直方图。优化器可以通过它获得更好的选择率计算。
# endpoint counts是如何工作的
假设一开始有如下硬币
![coins](/images/histogram/GUID-DA6EA695-A380-47C7-87A9-BAA0EE496F84-default.png)
一开始要求按照三个bucket来等高分配，结果如下
![coins](/images/histogram/GUID-C9D37B2D-8021-4100-9639-2F625D7D81BD-default.png)
频率直方图要求把同值的一定要放大一个bucket中，我们再来按这个要求做一次
![coins](/images/histogram/GUID-275A7BFD-B6CB-45D5-A3AF-58686749A2B3-default.png)
我们再为bucket添加repeat count
![coins](/images/histogram/GUID-A7EA028E-C734-48D2-9E70-C86BA7F7A35B-default.png)
repeat count纪录的是endpoint value在bucket里面重复的次数。
## 构建混合直方图的原则
* NDV超过n
* 不符合构建top频率直方图的条件
* DBMS_STATS中的estimate_percent被设置成了AUTO_SAMPLE_SIZE
## 构建混合直方图
还是之前的数据，要求产生10个bucket的直方图，最终获得
```
SELECT ENDPOINT_NUMBER, ENDPOINT_VALUE, ENDPOINT_REPEAT_COUNT
FROM   USER_HISTOGRAMS
WHERE  TABLE_NAME='PRODUCTS'
AND    COLUMN_NAME='PROD_SUBCATEGORY_ID'
ORDER BY 1;
 
ENDPOINT_NUMBER ENDPOINT_VALUE ENDPOINT_REPEAT_COUNT
--------------- -------------- ---------------------
              1           2011                     1
             13           2014                     8
             26           2032                     6
             36           2036                     4
             45           2043                     3
             51           2051                     5
             52           2052                     1
             54           2053                     2
             60           2054                     6
             72           2056                     5
 
10 rows selected.
```
In a height-based histogram, the optimizer would evenly distribute 72 rows into the 10 specified histogram buckets, so that each bucket contains approximately 7 rows. Because this is a hybrid histogram, the optimizer distributes the values so that no value occupies more than one bucket. For example, the optimizer does not put some instances of value 2036 into one bucket and some instances of this value into another bucket: all instances are in bucket 36.

The endpoint repeat count shows the number of times the highest value in the bucket is repeated. By using the endpoint number and repeat count for these values, the optimizer can estimate cardinality. For example, bucket 36 contains instances of values 2033, 2034, 2035, and 2036. The endpoint value 2036 has an endpoint repeat count of 4, so the optimizer knows that 4 instances of this value exist. For values such as 2033, which are not endpoints, the optimizer estimates cardinality using density.
