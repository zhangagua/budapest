title: c++中的throw
date: 2016-08-01 04:04:22
tags: c++
categories: c++
---

c++里面有个throw，用于我们的异常处理。在c语言里面，传统的错误处理方式则是使用err code(return)来标识错误。

腾讯的面试官今天问我，你在c++项目中throw用的多么？我说我用的不多。
然后面试问我，那么你们平时使用的是？我说return err num。
继续问，那么两者你觉得有什么差别？

懵了一下。

好吧，其实我不是太记得throw的坏处了，大致就回答了一个说，throw在程序效率来说，更加的低一些，err num的话效率高些，但是对err num的设计要复杂些。
我自己非常非常不满意自己的回答。插句题外话，二面面试的时候我觉得自己没有一面状态好。一面的时候我展开的比较多，二面的时候，我心理总是觉得二面和一面面试官肯定有过交流，有很多重复性的语言潜意识里认为不需要讲了，所以每次展开都不够。

回过继续讲，err code这种方式一般需要放在一个文件中，这个文件基本会被所有的cpp文件所引用，我在项目中最痛苦的就是这种全局性头文件。一个全局头文件的更改，在编译的时候，会重新编译所有的cpp文件。如果项目文件特别特别多的时候，这样编译一次其实挺费时间的。

下来之后我再好好思考了一下，觉得错误/异常处理在之前的项目中一直处于比较忽略的地位。为了获得功能，我们的出错处理更多的是通过记录日志，调试时把日志定向到标准输出中。完全是一种处于把Debug、日志、程序错误/异常处理混为一谈的地步。然而在一个复杂的系统中，错误/异常处理是非常非常重要的，没有完善的一套错误/异常处理流程的系统是不能够上线的。

懵。

# 关于throw和errno的优劣

[谷歌的c++编程规范](https://google.github.io/styleguide/cppguide.html)里面关于throw的优势和劣势有如下的描述。

关于throw的优势：
>Exceptions allow higher levels of an application to decide how to handle "can't happen" failures in deeply nested functions, without the obscuring and error-prone bookkeeping of error codes.
(允许高层次的函数处理低层次函数的异常，同时避免了errno形式中的模糊和容易出错)
Exceptions are used by most other modern languages. Using them in C++ would make it more consistent with Python, Java, and the C++ that others are familiar with.
Some third-party C++ libraries use exceptions, and turning them off internally makes it harder to integrate with those libraries.
（异常这种方式大家都在用，c++也的很多库也在用这个，如果用这些库显然更建议使用throw）
Exceptions are the only way for a constructor to fail. We can simulate this with a factory function or an Init() method, but these require heap allocation or a new "invalid" state, respectively.
（__异常是处理构造失败的唯一方式__）
Exceptions are really handy in testing frameworks.
(在测试框架中异常真的得心应手！)

关于throw的劣势
>When you add a throw statement to an existing function, you must examine all of its transitive callers. Either they must make at least the basic exception safety guarantee, or they must never catch the exception and be happy with the program terminating as a result. For instance, if f() calls g() calls h(), and h throws an exception that f catches, g has to be careful or it may not clean up properly.
(一个函数throw，所有调函数的人都得处理异常,若没catch住则或者terminate终止程序)
More generally, exceptions make the control flow of programs difficult to evaluate by looking at code: functions may return in places you don't expect. This causes maintainability and debugging difficulties. You can minimize this cost via some rules on how and where exceptions can be used, but at the cost of more that a developer needs to know and understand.
(程序的流程难以通过查看代码来评估：异常会让函数可能在意想不到的地方返回。理解和调试的困难程度都增加了)
Exception safety requires both RAII and different coding practices. Lots of supporting machinery is needed to make writing correct exception-safe code easy. Further, to avoid requiring readers to understand the entire call graph, exception-safe code must isolate logic that writes to persistent state into a "commit" phase. This will have both benefits and costs (perhaps where you're forced to obfuscate code to isolate the commit). Allowing exceptions would force us to always pay those costs even when they're not worth it.
(异常安全需要同时具备两个条件：RAII和不同的代码实践。想要简单的就写出正确的异常安全代码，是需要大量的组件支持的。如果想要读代码的人不需要全局视野就能明白代码，异常安全的代码要逻辑独立。异常安全的代码将写入到持久状态的逻辑隔离到“提交”阶段中。说白了就是为了异常安全以及他的可读性，你会有很多overhead的额外工作。)
Turning on exceptions adds data to each binary produced, increasing compile time (probably slightly) and possibly increasing address space pressure.
(异常会增加编译时间，增加地址空间开销。 - -' 这劣势是不是有点吹毛求疵的意思？)
The availability of exceptions may encourage developers to throw them when they are not appropriate or recover from them when it's not safe to do so. For example, invalid user input should not cause exceptions to be thrown. We would need to make the style guide even longer to document these restrictions!
(异常会有一种负面效应：程序员写久了什么都抛出异常。比如用户登录输入错误这种，是不应该抛出异常的。 - -'' 额。。)

此外，风格指南之后还写了一段话。它描述了google为什么不愿使用异常。它表示在新的项目中，使用异常获得的好处是要比cost要更多的，也就是说新的项目是可以考虑使用异常的。但是对于google来说，它有大量的既有代码，而这些代码都是没有异常的，这是它不愿意使用异常的一个根本原因。讲真就是这个东西还是挺好的，鉴于过去代码的原因不能使用在旧代码上，但是未来的新项目可能考虑使用。

>This prohibition also applies to the exception-related features added in C++11, such as noexcept, std::exception_ptr, and std::nested_exception.

不怎么招人待见的throw在c++11标准中添加和补充了它相关的功能。wiki上甚至说到：“事实上，异常规格这一特性在程序中很少被使用，因此在C++11中被弃用。”C++11定义了新的noexcept关键字。如果在函数声明后直接加上noexcept关键字，表示函数不会抛出异常。

所以结论呢？各有千秋，对于程序员来说，萝卜白菜各有所爱。不过一个系统的话，最好还是采用统一的方式来捕获和传递错误和异常信息。
