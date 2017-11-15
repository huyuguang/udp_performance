# udp_performance

测试Linux下UDP性能

测试环境是用光纤相连的两台Linux服务器。两台服务器配置相同，E2630*2，每个CPU有6个core，因此开启超线程是24个逻辑CPU。10G网卡。
光纤之间的TTL是35.8ms。内核是4.13.5。

因为没有100G环境，所以测试的时候，UDP的payload用100，这样，一个以太网包长度是14+28+100=142，如果再加上MAC的前导码，一共是146字节。
payload长度作为参数可选。

为了模拟真实环境，接收方只用一个UDP PORT，发送方可以选择只用一个UDP PORT，也可以选择用多个UDP PORT。

经过一番RSS, RPS, RFS折腾，最后能稳定的跑到每秒处理550w个包。

注意里面有几个关键点：
1，SO_REUSEPORT
2，SO_REUSEPORT_CBPF
3，bind thread to CPU

实测在RSS调优的情况下，通过BPF绑定CPU，线程不绑定CPU的效果是最好的。
并不需要开启RPS和RFS。

RSS应该把queue数量设置为12（core number），而不是默认的16。

关闭irqbalance，用affinity_hint指定queue和CPU的关系（sudo irqbalance -h exact -o）。

https://blog.packagecloud.io/eng/2016/06/22/monitoring-tuning-linux-networking-stack-receiving-data/#ntuple-filtering-for-steering-network-flows

https://blog.packagecloud.io/eng/2016/10/11/monitoring-tuning-linux-networking-stack-receiving-data-illustrated/

https://blog.packagecloud.io/eng/2017/02/06/monitoring-tuning-linux-networking-stack-sending-data/

https://access.redhat.com/documentation/en-us/red_hat_enterprise_linux/6/html/performance_tuning_guide/network-rss

https://www.kernel.org/doc/Documentation/networking/scaling.txt

https://events.linuxfoundation.org/sites/events/files/slides/LinuxConJapan2016_makita_160712.pdf

还有一些方法可以进一步提高吞吐率：
禁用GRO
卸载iptables module
禁用source IP validation
禁用auditd
最后应该能让吞吐率达到每秒600w~700w个包。
不过后面这些改动对系统影响太大，除非是专用的内网设备，否则不值得这样折腾。
