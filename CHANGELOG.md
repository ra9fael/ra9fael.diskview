# Changelog

## [0.3.17] - 2026-11-18

### Fixed
- Build, test and package the host-Qt QML plugin in the release workflow, using its own manifest and QML branch identity.
- Validate remote version tags and QML package contents before publishing immutable release assets.

## [0.3.16] - 2026-11-18

### 修复
- 文件操作右键菜单保持正常横向宽度。
- 设置按钮文字水平、垂直居中；「应用」悬停时保持强调色。

## [QML 0.3.15] - 2026-11-18

### 修复
- 设置窗口与扫描设置对话框的「取消 / 应用」恢复为清晰可辨的次级描边与主操作按钮。
- 文件操作右键菜单统一背景、间距和菜单项，鼠标悬停、键盘高亮、按下与禁用状态都有可见反馈。
- 图表右侧的层级下拉菜单向左贴齐，选项不再被窗口边缘截断。
- 扩展名面板出现「清除选择」时，Dock 标签行保持固定高度，按钮文字垂直居中。
- 升级旧配置时为空排除列表补入固定盘的系统保留目录；初始化后尊重用户删除或清空的选择。

## [QML 0.3.14] - 2026-11-18

### 新增
- 插件有自己的设置窗口（`settingsWindow`）：卡片上的「设置」按钮和搜索框里的「磁盘透视设置」都打开它，里面是原来扫描设置对话框的全部内容，但不建扫描器、不读磁盘树。查看器工具栏的「扫描设置」显示同一份内容。
- 文件夹结果的右键菜单多一项「分析文件夹占用」（`files` 触发，只接受一个文件夹），路径原样进入已有的扫描流程，空格和中文路径不需要重新输入。

### 变更
- 宿主设置真正生效：初始化时应用宿主配置，`settingsChanged` 在 GUI 线程更新同一份设置对象，下一次扫描和已打开的窗口读到的都是同一个值。
- 公共项（默认视图、扫描方式、文件夹菜单开关）在设置里恢复可编辑，改完通过 `winshun/settings/update` 只回写动过的键；宿主下发不会再次回发。并发上限、介质策略、预览间隔移出清单，改由插件保存；宿主仍带着以前存下的值就照用。
- 默认视图与窗口当前视图分开：默认值只决定下一次打开用哪种画法，浏览时切换视图不再隐式改动它。
- 设置改成只提交实际编辑的字段；「确定后立即重新扫描」勾选项换成独立的「用新设置重新扫描」按钮，没有目标或扫描进行中时不可用，应用本身从不触发扫描。
- 打开中的窗口收到新的扫描请求时，若扫描器正忙会明确报告失败，而不是先改掉上次路径再报成功。

## [QML 0.3.8] - 2026-11-16

### 修复
- Dock 控件全屏后能找回出口了：全屏层顶部加了一条聚焦栏，左侧显示当前面板名，右侧是「还原」按钮（corners-in 图标，Esc 仍可用）；预设布局的面板标题栏本来就有还原按钮，聚焦栏只在 Dock 模式出现。
- 列显示 rail 按钮真正 toggle：再点一次关闭菜单，点其他表的按钮切换面板；菜单打开时点击窗口其他位置先关闭（不穿透触发下方控件），Esc 也可关闭。自检新增 `columns-menu-toggle`（含真实鼠标点击路径）。
- 四个弹窗（扫描设置、主题设置、扫描问题、删除确认）的按钮行从自定义 `footer` 移进内容区并换用 ThemedButton：footer 不计入弹窗隐式高度，按钮会压出或盖住边框。
- Dock 工作区背景改用不透明 solidSurface，Dock 布局几何取整到整像素：Mica 下不会再从面板与状态栏之间的空隙透出底层窗口材质或像素。
- 工具栏「主题设置」从纯图标按钮改为图标+文字按钮，与「扫描设置」一致。

## [QML 0.3.7] - 2026-11-16

### 修复
- 控件风格对齐 WinShun：新增 ThemedButton，扫描/暂停/继续/取消/扫描问题/扫描设置按钮改用统一描边按钮并带 Phosphor 图标；下拉框与 palette.base/button 改用 control 系配色，Mica 下不再是半透明平底。
- 列显示改为每个列表各自独立：树、文件、扩展名三表右侧各加固定 rail 与 list-numbers 按钮，打开各自的列菜单；菜单不再随鼠标移出自动关闭；name/size 锁定不进菜单。旧配置里共享的隐藏列自动迁移为文件视图自己的隐藏列。
- Dock 模式删除标签栏下方的折叠/最大化 bar（各面板标题栏在 Dock 下收起）；文件视图筛选栏与扫描来源（当前/整个扫描、含子目录、子串/模糊）合并为一条工具栏，删除筛选栏右侧的放大缩小按钮。
- 图表展开文件夹的左右颜色条改为中性结构色，不再读作多一个文件块（磁贴新增 expanded 标志，pass 1 填 chrome 色）。
- 放大后的标签栏与悬停详情卡片改用不透明 solidSurface，Mica 下不再透出背景。
- 列表网格描边淡化（alpha 0.5 → 0.3）。

## [QML 0.3.6] - 2026-11-16

### 修复
- 下拉框文本不再截断：控件改为按最长选项自适应宽度，并清掉 Basic 样式自带的左右内边距，避免文字区被双重留白挤没；磁盘切换弹窗按最长行文本自适应宽度。
- 滚动条闲置后自动淡出，悬停时才显现。
- Dock 模式下各面板标题栏不再重复显示面板名，标签栏与标题栏二选一。
- 树视图默认列宽收紧到窗格宽度以内，首次打开名称列不再被压缩遮挡；拖动与自适应逻辑不变。
- 主窗口在满足条件时启用 Windows 11 Mica/亚克力背景（跟随宿主透明效果与 uiStyle），软渲染、旧系统或高对比度下自动回退为不透明配色；弹出窗口不启用。

## [QML 0.3.5] - 2026-11-16

### 修复
- 左上角切换磁盘恢复为下拉框（ComboBox），不再随鼠标移出自动关闭。
- 插件回退配色改为 WinShun 默认主题：宿主未提供 token（独立运行、本地浅色/深色模式）时改用 fluent Look 的中性色，替换旧的自拟蓝灰暗色；danger、tagFill、菜单与窗口边缘色、圆角（控件 4 / 卡片 6 / 小件 3）全部对齐。
- 部分宿主 token 缺失时菜单背景、悬停与边缘色也回退到 WinShun 默认值；图表弱化色改为中性灰。

## [QML 0.3.4] - 2026-11-16

### 修复
- 允许操作扫描根目录；根目录删除成功后清空旧快照并自动扫描上级目录，同时保留系统路径保护。
- 扩展名视图的“扩展名”列可拖动和双击自适应。
- 窄 Dock 面板的列宽缩放不再写回用户宽度设置；调整只影响目标列和相邻列。
- 磁盘菜单和列显示菜单在鼠标移出后延时关闭。
- PaneHeader 控件按可用高度收缩；短文件视图隐藏次级工具栏，避免筛选框与下方行挤压重叠。

## [QML 0.3.3] - 2026-11-16

### 修复
- Dock 方向罗盘按目标标签组显示，全部标签合并时也能看见拆分方向。
- 列菜单锚定在文件视图工具栏，不再弹到窗口左上角。
- 名称/文件名筛选改为文件视图独立状态，不再影响树视图。
- 点击扩展名只高亮图表中对应类型，不再筛选文件列表。
- Dock 标签统一使用“树视图/文件视图/可视化视图/扩展名视图”命名，移除选中态短蓝条。

## [QML 0.3.2] - 2026-11-16

### 修复
- 正文与 chrome 按 WinShun `background`/`layer` 语义绘制，主题颜色和控件边框保留宿主 token。
- 全面板合并为同一组标签后，活动标签也可以再次拖出拆分。
- Dock 面板偏移统一使用主题行高。
- 列宽拖动不设最小值；固定表宽，相邻列吸收宽度，窄面板按比例压缩列。
- 加宽百分比列默认宽度；磁盘菜单改为足够宽的固定宽度。

## [QML 0.3.1] - 2026-11-16

### 修复
- Dock 选中标签改为填充背景高亮，不再显示方框边线。
- 百分比列改为低透明度整格数据条并将文字置顶；缩窄百分比列默认宽度。
- 浅色界面增强表面、分隔线、图标和滚动条对比度。
- 列宽改为拖动后向右推移后续列；双击任意列分隔线按内容自适应。
- 恢复标准磁盘选择菜单；地址建议仅在输入或按下方向键请求时显示。
- 移除孤立“搜索：”行，将列菜单与筛选框移至树视图标题栏。
- 图表导航与面包屑、树视图当前目录保持同步；固定面包屑工具栏高度。
- 子文件夹标题条与边框在未选中状态下保持清晰，较大的折叠文件夹也有常态外框。

## [QML 0.3.0] - 2026-11-16

### 新增
- 主题设置可调整基础字号、树行高、文件行高和表头行高，默认使用更紧凑的布局。
- 三张列表补齐垂直滚动条；选盘弹层支持滚动、键盘选择和移出自动关闭。
- 面板标题栏提供折叠、最大化和还原，双击标题栏也可切换最大化。

### 修复
- 选中图表元素时标题保持中性，不再反色。
- 修复整盘扫描时回收站和永久删除被错误禁用的问题。
- 百分比列恢复可见的条形进度；自动层级在钻入目录后继续展开可读子项。
- 列名拖动改为弹性名称列，修复名称列拖动异常，并统一表头、内容和滚动宽度。

## [QML 0.2.0] - 2026-11-16

### 新增
- 右键菜单补齐打开、复制路径、复制/剪切文件、资源管理器定位、树内定位、属性、移至回收站和永久删除（需输入名称确认）。
- 地址建议支持异步检索、键盘选择，空输入时提供快速位置。

### 变更
- 跟随 WinShun 外观令牌、字体和尺寸；移除重复的窗口内标题。
- 文件树固定按占用降序，文件列表保留独立排序。
- 名称列可调宽；拖动任意列时固定名称宽度，超出面板时横向滚动。
- 图表按层级预算保留余量，并为图表操作提供绝对路径。

### 权限
- 新增 `files:write`，用于移入回收站和永久删除。升级时 WinShun 会要求确认。

[English](CHANGELOG.en.md)

All notable changes to this plugin. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions follow
[Semantic Versioning](https://semver.org/). The version in `plugin.json` is the
single source of truth; release tags are `v<version>`.

## [0.3.0] - 2026-11-09

### 鍙樻洿
- 鐣岄潰涓?HTML 浜や簰鍘熷瀷閫愰」瀵归綈锛氬瓧浣撴敼涓哄儚绱?token锛堟鏂?13 / 寮鸿皟 13 鍗婄矖 / 娆¤ 12 / 鍥惧潡 11锛夛紝鍥捐〃鏍囬浠?20px 闄嶄负涓ょ骇锛涘垪琛ㄥご鍚堝苟涓轰竴琛岋紙妯″紡鍒囨崲涓庣瓫閫夋鍚屾帓锛夛紝琛岄珮 28px锛屽垪澶翠笁鍒楃嫭绔嬪苟涓庢暟鎹榻愶紱鍥捐〃鍐呬笉鍐嶅弻閲嶅唴缂╋紝鍙敤楂樺害鎻愬崌鍒扮害 81%锛?184脳761 瀹炴祴 613px锛夈€?- 缂╂斁涓庡钩绉讳笉鍐嶅崰鐢ㄦ寜閽細婊氳疆銆佺┖鏍兼嫋鍔ㄣ€佷腑閿嫋鍔ㄥ拰 `0` 閿鐩栧悓鏍疯兘鍔涳紝鍥捐〃鍐呬笉鍐嶆湁缂╂斁鍜屽钩绉绘寜閽锛屼篃涓嶅啀鍗犵敤鍥惧唴鏍囬鍖恒€?- 鐘舵€佹爮绗竴琛屾樉绀洪€変腑椤瑰悕绉般€佸畬鏁磋矾寰勩€佸ぇ灏忎笌鍗犳瘮銆佽鍙栭棶棰樺窘鏍囷紝浠ュ強杩涘叆 / 瀹氫綅 / 鏇村涓変釜鎸夐挳锛涚浜岃鏄剧ず蹇嵎閿彁绀哄拰鍥捐〃鍍忕礌鍗犳瘮銆傜灛鏃舵秷鎭敼涓哄浘琛ㄥ尯鍙充笅瑙掔殑 toast锛? 绉掕嚜鍔ㄦ秷澶憋紱鎵弿鐘舵€佸彧鍦ㄥ窘鏍囦竴澶勮〃杈撅紝涓嶅啀閲嶅鍑虹幇鍦ㄦ爣棰樻爮涓庨《閮ㄦ暣琛屻€?- 鈥滃叏閮ㄤ笂绾р€濅粠鐙珛鎸夐挳鏀逛负鍛戒护琛ㄦ潯鐩紙`Command::Ancestors`锛夛紝鍙粠鍙抽敭鑿滃崟鍜?Ctrl+K 鎿嶄綔鎼滅储璋冪敤銆?- 绐勭獥鍙ｄ笅鍥捐〃宸ュ叿鏉″垎涓よ锛氱涓€琛岀埗绾ф寜閽笌褰撳墠鐩綍锛岀浜岃瑙嗗浘鍒囨崲锛涚姸鎬佹爮鍚屾牱鍒嗕袱琛屽爢鍙犮€?
### 鏂板
- 鏅€氭壂鎻忔敼涓烘湁鐣岀洰褰曞伐浣滄睜锛坄parallel_scan.h`锛夛細鍥哄畾鏁伴噺鐨?worker 鍙骇鍑哄眬閮ㄧ粨鏋滐紝鐢卞崟涓€鍗忚皟绾跨▼鎸佹湁骞朵慨鏀规爲锛屼笉瀛樺湪澶氱嚎绋嬪悓鏃舵敼鍐欏悓涓€鑺傜偣銆傝嚜鍔ㄧ瓥鐣ヤ粠 SSD 4銆佹満姊扮洏 1銆佺綉缁?2銆佹湭鐭ヨ澶?1 璧锋锛涙壂鎻忚缃彁渚?1 / 2 / 4 / 8銆傚苟鍙戞洿楂樹笉鎵胯鏇村揩銆?- 鎵弿璁剧疆闈㈡澘锛氭壂鎻忔柟寮忋€佸苟鍙戜笂闄愩€佷粙璐ㄧ瓥鐣ャ€侀瑙堝彂甯冮棿闅斾笌涓嬭疆鎺掗櫎銆傝繖浜涜缃幇鍦ㄥ啓鍏?diskview.json锛屼笅娆″惎鍔ㄦ仮澶嶏紱鎵弿澶辫触鎴栧彇娑堜篃涓嶄細涓㈠け銆?- 鎵弿涓彲鏆傚仠 / 缁х画锛汧5 鎵撳紑鎵弿璁剧疆锛岃€屼笉鏄潤榛橀噸鎵€?- 鐖剁骇瀵艰埅涓夊鍏ュ彛锛氬綋鍓嶇洰褰曟梺甯︽爣棰樼殑鈥溾啈 杩斿洖鈥濄€佸垪琛ㄥ浐瀹氶琛屸€?.鈥濄€佸渾鐩樹腑蹇冦€傝繑鍥炲悗浼氶噸鏂伴€変腑鍒氱寮€鐨勭洰褰曞苟鎭㈠鐒︾偣銆?- 姣忕洰褰曡鍥捐蹇嗭細閲嶆柊杩涘叆鏌愮洰褰曟椂鎭㈠閫変腑椤广€佺瓫閫夈€佹粴鍔ㄤ綅缃拰鍒楄〃妯″紡銆?- 鍥捐〃缂╂斁锛?5% 姝ヨ繘鑷?400%锛夈€佸洓鍚戞湁鐣屽钩绉汇€佺┖鏍兼嫋鍔ㄤ笌涓敭鎷栧姩銆侀€傚悎绐楀彛澶嶄綅锛涘懡涓祴璇曡鍏ョ缉鏀惧拰骞崇Щ銆?- 缁熶竴鍛戒护琛紙`viewer_commands.h`锛夛紝鍙抽敭鑿滃崟銆佹搷浣滄悳绱紙Ctrl+K锛変笌閿洏鍏辩敤鍚屼竴濂楀彲鐢ㄦ潯浠朵笌鎵ц鍏ュ彛銆傚彸閿彍鍗曟寜鈥滄枃浠舵搷浣?/ 瀵艰埅 / 鎵弿鈥濆垎缁勶紱鍥捐〃鏉＄洰銆佸垪琛ㄨ鍜岀姸鎬佹爮鍏辩敤鍚屼竴鑿滃崟锛屾敮鎸佸彸閿笌 Shift+F10銆?- 鏂囦欢鎿嶄綔琛ラ綈锛氱Щ鑷冲洖鏀剁珯锛圛FileOperation锛夈€佸鍒?/ 鍓垏鏂囦欢锛圕F_HDROP + Preferred DropEffect锛岃祫婧愮鐞嗗櫒绮樿创鐢熸晥锛屽揩鎹烽敭 Ctrl+Shift+C / X锛夈€佹案涔呭垹闄わ紙涓嶇粡杩囧洖鏀剁珯锛屽厛闃呰璀﹀憡鍐嶈緭鍏ユ潯鐩悕绉扮‘璁わ紝鏃犳硶鎾ら攢锛屼笉璁惧揩鎹烽敭锛夈€傛壂鎻忕粨鏋滀笉瀹屾暣涓嶅啀闃绘鍒犻櫎锛涙壂鎻忚繘琛屼腑涓庣郴缁熷彈淇濇姢椤逛粛鎷掔粷銆傚彧鏈?shell 纭鎴愬姛鍚庢墠鏇存柊蹇収銆傛柊澧?clipboard:write 鏉冮檺锛屽畨瑁呮椂闇€纭銆?- Direct2D 鎶楅敮榻垮渾寮у嚑浣曪紙`viewer_drawing.h`锛夛紝璁惧鎴栧伐鍘備笉鍙敤鏃跺洖閫€ GDI銆?- 鎵弿闂銆佸閲忕粺璁″彛寰勪笌闃舵璁℃椂鏀逛负闈炴ā鎬侀潰鏉匡紝闃呰鏃朵粛鍙鑸€?- 灞€閮ㄩ瑙堜腑绛夊緟鐖剁骇鍏宠仈鐨?MFT 璁板綍璁″叆 `pendingRecords`锛屾壂鎻忓畬鎴愬悗鎵嶅綊鍏?`orphanRecords`銆傝鎺掗櫎鐨勫瓙鏍戣鍏?`excludedItems`锛屼笉鍐嶇畻浣滆鍙栭敊璇€?
### 淇
- 绛涢€夋鐨勬彁绀烘枃瀛楀湪鎵弿鍜屼氦浜掓湡闂存寔缁棯鐑侊細涓荤獥鍙ｈˉ涓?WS_CLIPCHILDREN锛屾彁绀烘枃瀛楁敼涓轰笌鎺т欢鍚岃稛缁樺埗锛岀Щ鍔ㄥ瓙鎺т欢涓嶅啀瑙﹀彂鏃犺皳閲嶇粯锛涜仛鐒︽椂鎻愮ず鏂囧瓧鎸夌郴缁熶範鎯殣钘忋€?- 鎵弿璁剧疆姣忔閲嶅惎閮戒細涓㈠け锛屽洜涓?diskview.json 鍙繚瀛樹簡涓婃璺緞銆?- 瀹归噺涓庤鐩栬寖鍥村璇濇鏄ā鎬佺殑锛屼細闃诲鐖剁骇瀵艰埅銆?
## [0.2.0] - 2026-11-09

- 淇鍙墽琛屾枃浠跺浘鏍囷紝琛ラ綈鎵撳寘涓殑 ICO 鍜岃祫婧愯剼鏈€?涓虹獥鍙ｆ樉寮忚缃ぇ灏忓浘鏍囷紝闅?DPI 鏇存柊骞堕噴鏀惧浘鏍囧彞鏌勩€?鍚屾椂鍖呭惈褰撳墠宸ヤ綔鍖虹殑 0.2.0 鎵弿鍜岀晫闈㈡敼鍔紱鏂板 processes 鏉冮檺锛屽崌绾ч渶瑕佺‘璁ゃ€?- 鏈浠呯敓鎴愭湰鍦版爣绛句笌瀹夎鍖呫€傚疄闄呴獙璇佽寖鍥磋 docs/icon-validation.md锛涙湭杩涜 GitHub 鍙戝竷銆傛洿鏂板悗闇€閲嶆柊鍚姩鎻掍欢杩涚▼銆?
### Added
- Optional read-only MFT scanning for whole fixed NTFS drives, using a
  short-lived elevated helper and a restricted local metadata pipe. The UI
  stays unelevated; declined consent or helper failure falls back to standard
  enumeration with a visible reason. Adds the `processes` permission.
- Top breadcrumbs, separate back/forward history and parent navigation,
  editable indexed paths with Ctrl+L, persistent single-click selection and
  double-click folder navigation.
- Delayed full-path hover overlays, matching status text, synchronized chart
  and list highlighting, selection details and a locate-parent action.
- One/two-level layouts, small-item aggregation, resizable panes, narrow-window
  chart/list tabs, filtering, sorting, pagination and up to 1000 largest files.
- Nonmodal scan progress, immutable partial previews and previous-result
  recovery; incomplete results, unknown sizes and failures stay explicit.
- Keyboard focus navigation, a standalone `--view [path]` preview mode,
  updated light/dark styling, executable icon and version resources.
- MFT/parser/transport and native interaction regressions; the distribution
  includes the vendored WinShun reader's MIT license and all required sources.
- Fixed-drive capacity and usage in query results and clickable welcome cards.
- A Folders tab with immediate subfolder sizes and shares, including empty
  folders; the largest-files ranking now follows the current folder.
- Clickable skipped-directory details in the status bar.
- Window-message regression tests, deterministic visual snapshots, and a
  real-process protocol smoke test for scan, reopen, theme updates and shutdown.

### Fixed
- Drive-root and extended-path normalization, relative paths and forward slashes.
- Navigation through nested tiles, root-level file rows, breadcrumbs and Up.
- Failed scans expose the error while retaining an explicitly identified
  previous result; cancellation preserves complete or labelled partial data.
- Sunburst rings stay inside the chart at small sizes and high DPI; status
  text no longer overlaps breadcrumbs or totals.
- The path box follows drive-card scans; F5 also works from the path box.
- Reopening clears previous errors and cancellation state; shutdown destroys
  pending scan messages using their correct types.


## [0.1.0] - 2026-11-08

### Added
- Initial release. `disk` opens the disk view window; `disk <path>` opens and
  scans that folder immediately. Scanning is always user-triggered and only
  happens in the window.
- Recursive treemap (squarified, default) and recursive sunburst, switchable
  in the window (Ctrl+V) and via the `defaultView` setting. Culled entries
  leave honest gaps; parent totals are never redistributed.
- Drill down by clicking tiles / slices, go up with Backspace or the Back
  button, jump via status-bar breadcrumbs.
- "Largest files" list (up to 64 rows) with click-to-folder navigation.
- Hover details: kind, size, file/folder counts, unreadable entries, and a
  cluster-aligned allocation estimate for files.
- Scanner: cancellation with previous-result retention, per-node unreadable
  accounting (up to 32 paths listed), reparse points counted but never
  followed, `\\?\` long-path and UNC support, logical vs cluster-rounded
  sizes.
- The window paints itself with the host's own colours (`appearance.nativeLook`),
  falling back to built-in light and dark palettes; it follows a theme or style
  change while it is open.
- The `defaultView` setting decides the startup view; the saved config keeps
  only the last scanned path (`diskview.json` in the plugin data directory).
- Per-monitor V2 DPI awareness.
