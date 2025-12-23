# Kindle 阅读统计应用

一个基于 GTK2 的 Kindle 设备阅读时长统计工具，可显示详细的阅读数据可视化图表。

# 功能特性

· 概览页面：显示今日阅读总时长和累计阅读总时长
· 今日分布：以柱状图展示当天每 2 小时的阅读时长分布，并标记最佳阅读时间段
· 本周分布：以柱状图展示一周内每天的阅读时长分布，显示本周阅读最多的一天
· 月视图：日历式布局展示整月每天的阅读时长，黑色格子表示阅读超过 30 分钟
· 自动数据收集：自动解析 Kindle 设备中的阅读日志文件
· 跨月浏览：支持查看任意月份的历史阅读数据

# 项目关联

本项目是 Kykky 项目的组成部分。Kykky 提供KUAL菜单项，让本项目可以与KUAL集成。

# 安装与使用

注意：完整的安装步骤、依赖项和配置说明，请参考主项目文档：

👉 前往 Kykky 项目主页查看详细安装指南[https://github.com/TQHYG/kykky]

# 数据来源

应用自动读取 Kindle 设备中 /mnt/us/extensions/kykky/log/ 目录下的 metrics_reader_* 日志文件，解析其中的阅读活动数据，无需手动记录。

# 编译说明

项目使用 Meson 构建系统，需要在 Kindle 开发环境或交叉编译环境中构建。

详细见 https://kindlemodding.org/kindle-dev/gtk-tutorial/prerequisites.html
