title: 戏说paxos
date: 2016-03-14 11:37:44
tags: 分布式系统
categories: 分布式一致性算法
---
## 小小的八卦
paxos算法在分布式领域算是很出名的一个算法了。它著名的其中一个原因是它发表的历程很有趣，Lamport大牛任性的故事。paxos的第一篇论文产生于1989年，但是最后是在1998年才发表出来。原始的论文是通过古希腊议会的故事来阐述的，投稿之后被回绝了，理由是：（1）通过故事的阐述篇幅过长（2）数学公式太少。理解起来有困难而且不是很规范。在96年的时候，Butler Lampson（另一个大牛），在自己的论文中提到这篇未发表出去的论文，而且算是比较隆重的向整个学术界推销了这篇论文。当时正好有需要这样一个协议的迫切需求，于是人们开始正视这篇论文。98年发表后，关于研究paxos的其他论文也相继发表，而Lampart大牛也开始着手将paxos用更加简单的方式表述出来，相继发表了《The ABCD’s of Paxos》，2001年又发表了《Paxos Made Simple》。之后google发表的论文透露了已经将paxos实践到了自己的系统中，google走在大家的前面，将理论中的paxos落地到了自己的工程中。其实现实中的分布式环境肯定是没有理想中那么丰满的，加之Paxos的难理解，难实现，对Paxos进行工程上的实践本身就是特牛的一件事情。Paxos算法在解决分布式环境下的一致性问题中的重要地位已经被很多名人牛人评价过了，Paxos协议在学术界和工业界广泛的认可为Lampart大牛获得了通往图灵奖铺上了最后一级的阶梯——2013获得了图灵奖。
好吧，我是一个八卦的程序员，还是回到我们的正题吧。

paxos主要的解决的问题就是在分布式环境下的一致性问题。在正式开始分析paxos协议之前，先来列一下paxos协议被用在哪些地方了。谷歌Chubby使用的就是paxos的修改版，spanner中有paxos，阿里的oceanbase也有。一般来说工程上不是用的原版的paxos，使用的是优化过或者称为简化过得paxos。这是spanner的文摘。
>在spanner的每个Tablet上会有一个Paxos状态机。Paxos是一个分布式一致性协议。Table的元数据和log都存储在上面。Paxos会选出一个replica做leader，这个leader的寿命默认是10s，10s后重选。Leader就相当于复制数据的master，其他replica的数据都是从他那里复制的。读请求可以走任意的replica，但是写请求只有去leader。这些replica统称为一个paxos group。
顺便说一句，spanner的全球时间同步。这个全球时间同步机制是用一个具有GPS和原子钟的TrueTime API提供的。推测应该是硬件加软件的形式。精确各个数据中心的偏差在10ns以内。
引用自：[这里](http://www.yankay.com/google-spanner%E5%8E%9F%E7%90%86-%E5%85%A8%E7%90%83%E7%BA%A7%E7%9A%84%E5%88%86%E5%B8%83%E5%BC%8F%E6%95%B0%E6%8D%AE%E5%BA%93/) 

再看Oceanbase的相关文摘。
>Oceanbase包含多个 Rootserver，同一个时刻只允许有一个Rootserver作为主Rootserver提供服务．当主出现故障或者整个集群重新启动时，Oceanbase包含多个通过分布式选举协议选出唯一的主Rootserver提供服务．分布式选举协议往往基于 Lamport提出的Paxos协议......oceanbase集群采用ntp协议来做时钟同步，大部分情况下成员之间的时钟偏差都在几毫秒之内，最坏情况下也不会超过100ms，从而保证选举协议是可终止的。
摘自《Oceanbase高可用方案》（作者为杨传辉）

---
## 细谈paxos

摆明了重要性之后，接下来就是开始分析这个非常有“意思”的协议了。我只想通过自己的理解来解释这个协议，所以不一定是非常的严谨。分析的过程主要参看的文献包括两个：
1. 《paxos made simple》
2.  阿里核心团队博客（[这里](http://csrd.aliapp.com/?p=160)）

阿里的博客其实也是跟着《paxos made simple》的（翻译）思路在走，加上了逻辑推理，同时自己给例子向读者阐述。前面我们已经说了，paxos没有数学推理，走的是逻辑推理证明的道路，所以我走的方式就是先理清楚逻辑推理的过程。引用一句来自阿里云栖社区关于paxos协议的博客（[浅谈分布式系统的基本问题：可用性与一致性](https://yq.aliyun.com/articles/2709)）：我唯一不保证的是正确性。因为这些协议的证明都是非常严谨的，但是如果用自己的话阐述出来，有可能就不是那么正确了，大牛说不正确可能是谦虚，如果小菜如我说不正确的话，那就可能是真的不正确了，哈哈。
顺便说一句，阿里云栖社区的文章真的干货特别多，而且涉及到方方面面，有事没事大家多去逛一逛，极有可能有非常大的收获！这片文章也是在这片博客快要写完的时候才找到，看完之后犹如醍醐灌顶，有好多我的疑惑都被提及了！

首先提出问题
>分布式环境下的一致性问题（落地：一个例子，多台服务器中相互协商选master）
>附加条件：这个一致性问题最终获得一个唯一的结果（落地：只有能选出一个master）

解决问题的方式
> 第一种方式：单个批准者，采用独裁的方式，由他说了算。
> 第二种方式：多个批准者，相互协商。

第一种方式可靠性上不去，因为一旦唯一的仲裁者挂掉那么就没有办法进行一致性问题的裁决。所以大家更愿意选第二种方式。第二种方式就涉及到角色的问题了，因为不是每一个机器都是批准者。我们使用批准者（Accepter）和提案者（Proposer）两个名称来区分，一台机器（或者说进程）既可以是批准者也可以是提案者，而一次投票或者选举某人某值，我们称为提案。既然是多人协商，那么涉及**三个问题**：第一个是协商的承载方式（异步通信），第二个是批准的规则（提案者的提案在什么情况一个批准者批准，什么情况下一个批准者拒绝），第三个是协商结果的断定规则（多个批准者可能产生多个批准结果，那么最终我们以哪个结果作为唯一的结果呢？）。第一点我们不用讨论了，我们从第三个问题入手开始反推。
既然有多个批准者，那么一个很自然断定结果的方式就是：
>A：超过半数批准者同意，这通过该提案。（属于结果的断定规则）

我们最常规的逻辑规则中，超过一半我们就认为是大多数人所期望的结果，这个是很好理解的。但是，如果一个批准者可以批准多个提案，那么超过半数的提案就可能不唯一了。违反了一致性问题协商的结果一定要是唯一的要求。为了唯一，我们尝试补上一个规则。
>B：每个批准者只能批准一个提案（以下的讨论都是在考虑批准规则）

还就是在算法的开始阶段，我们需要一个最简单的规则：
>C：（原则1，亦称**P1**）：每个批准者批准他收到的第一个提案

为什么？算法最开始时，某个批准者收到一个提案，你是批准呢，还是等待呢？假设永远没有其他提案到来了呢？于是保险的做法，对于一个批准者来说，第一个到达的提案，就直接批准。
整理上面的推理过程
>============《断定规则》=========《批准规则》==================================================                   
不能独裁==>多数同意则通过==>批准者只能批准一个提案==>（P1）批准者必须通过它收到的第一个提案

但是，这样的规则是隐藏有问题的。只能批准一个提案＋第一个到达的立即批准这样的原则会带来问题。
假设批准者有a1，a2，a3三个，提案者有pro1，pro2，pro3三个，算法的最开始三个提案的内容为key s = value_pro1，key s = value_pro2, key s = value_pro3。如果a1收到 s = value_pro1，a2收到s = value_pro2，a3收到s = value_pro3，于是就悲剧了。多个提案者提出多个值，但是遗憾的是没有任何一个值被多数派同意了。论文中Lampart说的是：p1和多数派同意才能决定一个提案的通过暗示：一个批准者必须要能批准多个提案。**这里我异常的困惑，Lampart并没有在论文中明确地提到活锁问题， 在阿里的博客的最后一段中，作者提到了类似的问题，作者把它归为工程中的实践问题，而且没有做详细的讨论。对这个问题的说明，后来读了一些博客之后有一些结论，我把它放到文章的最后来讲**。

>============《断定规则》=========《批准规则》==================================================                   
不能独裁==>多数同意则通过==>批准者可以批准多个规则==>（P1）批准者批准它收到的第一个提案
                                                  ==> ?

这里的问号代表我们在这样的断定规则和批准规则下，我们还需要其他的原则才能达到目的。
为了实现批准者可以批准多个提案，我们要给每一个提案带上一个编号，来标识这些提案，偏序的编号是可以非常容易被实现的。
删繁就简，先把接下来的几条逐渐被加强的规则列出来：
>原则**P2**：一个值为V的提案被通过了，那么编号比这个提案大的提案通过的条件是提案的值是V。
>P2的加强**P2a**：一个值为V的提案被通过了，那么编号比这个提案大的提案*被批准*的条件是提案的值是V。
>P2a的加强**P2b**：一个值为V的提案被通过了，那么编号比这个提案大的提案的值应该是V。
>P2b的加强**P2c**：提出一个编号为n具有值v的提案的前提是：存在一个多数派，要么他们中没有人批准过编号小于n的任何提案，要么他们批准的提案中编号小于n的最大的提案值是v。

注意P2和P2a的细微区别，于是我们刚才的那个推理过程变成了下面这样：

>============《断定规则》=========《批准规则》==================================================                   
不能独裁==>多数同意则通过==>批准者可以批准多个规则==>（P1）批准者批准它收到的第一个提案
                                                  ==>（P2c）...

所谓的加强就是指满足P2c则一定满足P2b，满足P2b则一定满足P2a，以此类推。**当我第一次读到这里的时候，我就一直疑惑，按照P2c这种方式，岂不是只要选出来一个值V，那么之后所有的提案岂不是一直都是选V？假设我们某时刻通过了ip为192.168.1.3的机器为master，那以后大家能提出来的提案master的值都为192.168.1.3？就算它挂了也要选它么？好吧，难道这也是一个工程问题？**这个时候再回头看看之前谷歌和oceanbase的文摘你就明白了，选master可以是等到租约期过了重选，也可以是等待主master挂了重选。但是我还是没有看出来那paxos算法的核心价值和伟大之处到底在哪里?
疑问先暂时保留着，先把paxos完整的算法流程提出来。
一个决议分为两个阶段:

> - prepare阶段：
>  - 提案者选择一个提案编号 n 并将 prepare 请求发送给批准者中的一个多数派
>  - 批准者收到 prepare 消息后，如果提案的编号大于它已经回复的所有 prepare 消息，则批准者将自己上次的批准回复给提案者，并承诺不再批准小于 n 的提案
> - 批准阶段：
>  - 当一个提案者收到了多数批准者对 prepare 的回复后，就进入批准阶段。它要向回复 >prepare 请求的批准者发送批准的请求，包括编号 n 和根据 P2c 决定的 value（如果根据 >P2c 没有决定 value，那么它可以自由决定 value）
>  - 在不违背自己向其他提案者的承诺的前提下，批准者收到批准请求后即批准这个请求

算法流程加上p1和p2c两个原则，就是整个完整的paxos协议了。阿里的博客中写到“这个过程在任何时候中断都可以保证正确性...保证了多种情况下协议的完备性。在异步通信的环境下，允许信息的丢失，仍然保证了一致性“。简洁却又有强大的功能。用自己的理解来来总结paxos：批准者批准收到的第一个提案，之后批准者只受理比他此前看到的编号更大编号提案：（a）如果此前它没批准过任何提案，那同意提案者的提案内容进入第二阶段（b）如果此前已经批准过某个提案，那么批准者告诉提案者你可以进入第二阶段，但是提案内容必须更改，把value改成之前我批准那个即可。


尽管如此，paxos的局限性仍然要提出来：
>活锁问题，算法不能保证终止
>第一个被批准的提案值v，永远不会被改变

前文中提到的[浅谈分布式系统的基本问题：可用性与一致性](https://yq.aliyun.com/articles/2709)还介绍了ZooKeeper的ZAB协议，ZAB协议解决了上面这两个问题。关于活锁的问题，其实是有引申的。有一个叫FLP Impossibility的证明，它揭露了分布式领域一个深刻的结论：在异步通信场景中，没有任何算法能保证到达一致性。所以paxos也存在这些必然会存在一些问题（活锁），如果paxos真的是完美的，那也就推翻了这个理论。被批准的提案值永远不会被改变，应该称为特性吧，也不能叫局限性，只是暂时还没有想到怎么利用这种特性。
