# 实验名称

观测分析性能

# 实验环境

- PC配置：

```
DESKTOP-LOQAKP6
处理器：12th Gen Intel(R) Core(TM) i5-12400F   2.50 GHz
RAM：32.0 GB
系统：Windows 11 专业版 23H2
显卡：NVIDIA RTX 3070 Ti

Docker version 26.0.0, build 2ae903e

Openstack-swift

python-swiftclient
```

- 远程服务器配置：

远程服务器地址：`blog.lushuyu.site`（我的博客服务器，临时征用过来做实验）

![](figure/1.png)


# 实验记录

分别测试本地、服务器的客户端数与上传下载平均时延的影响，以此作为benchmark，对比本地与远程的区别，使用pilot.py绘图。


本地测试文件：num_clients_local.py

服务器测试文件：num_clients_remote.py

- 本地测试结果：result_local.out

![](figure/num_clients_local---average_latency.png)

- 服务器测试结果：result_remote.out

![](figure/num_clients_remote---average_latency.png)

# 实验小结

可以看出来服务器的结果和网速关系很大，特别是读操作。