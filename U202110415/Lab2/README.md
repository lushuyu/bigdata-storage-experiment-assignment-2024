# 实验名称

实践基本功能

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

虽然在lab1里面我已经用swiftclient命令行实现了实践基本功能，但我还是写个Python文件系统测试一下。

`assets/test.py`
```python
# -*- coding: UTF-8 -*-

import swiftclient


endpoint = 'http://127.0.0.1:12345/'
_user = 'test:tester'
_key = 'testing'

conn = swiftclient.Connection(authurl=endpoint + 'auth/v1.0',user=_user,key=_key)
print("Connected successfully")


container_name = 'newBucket'
conn.put_container(container_name)

print("Created a new bucket")

# 上传一个文件 (Create)
object_name = 'testObj'
with open('./file1.txt', 'rb') as file:
    conn.put_object(container_name, object_name, contents=file.read())

print("Uploaded!")

# 读取一个文件 (Read)
response_headers, file_contents = conn.get_object(container_name, object_name)
print(file_contents)

# 更新一个文件 (Update)
object_name = 'testObj'
with open('./file2.txt', 'rb') as file:
    conn.put_object(container_name, object_name, contents=file.read())

print("Updated!!")

# 删除一个文件 (Delete)
conn.delete_object(container_name, object_name)

print("Deleted a file!!")

# 删除一个 bucket
conn.delete_container(container_name)

print("Deleted a bucket!!")
```

运行结果：

![](figure/2.png)


# 实验小结

非常简单~