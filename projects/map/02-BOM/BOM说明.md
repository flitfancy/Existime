# map 物料表说明

[map-BOM.xlsx](map-BOM.xlsx)包含charge充电版、battery电池版两张表；各版的`BOM.csv`是同内容的UTF-8表。charge为31组、145件，battery为22组、132件，按完整装配计算。完整丝印和无背面丝印共用对应版本的同一张表。

LED、电阻、电容、电感、自恢复保险丝和NTC按参数填写，不限定品牌、制造商料号或立创编号。芯片、MOSFET、USB连接器及开关保留准确型号，避免引脚或机械尺寸不匹配。0603、0805、1210等均按英制封装表示。

电阻保留阻值、精度、功率及原设计温漂；电容保留容量、精度、耐压和介质。battery输入/输出电容还需满足表中有效容量条件，不能只比较标称µF。功率电感保留电感量、容差、温升电流、饱和电流、直流电阻及焊盘要求；通用1008外形不保证焊盘相同。

LED按颜色和封装选料，Vf和亮度是原设计的参考参数。表内20mA是亮度参考的测试条件，不是板上实际灯电流；本板采用470Ω限流，需确认低电流时的亮度、颜色协调及极性。D1按白色0805 LED选料，不强制指定型号。

温升/饱和电流条件参考[功率电感原设计资料](https://www.murata.com/~/media/webrenewal/products/inductor/chip/tokoproducts/wirewoundmetalalloychiptype/m_dfe252012p.ashx)，NTC的R25和B25/85参数参考[原设计NTC规格](https://www.semitec-global.com/products/thermistor_at/)。其他通用件参数来自对应版本原生工程的器件属性。原生BOM保留原工程中的绑定，用于追溯；公开采购表以本参数表为准。

J2/SWD排针和TP1/TP2测试焊盘在原生工程中不加入BOM，本表同样不计入。用于自动PCBA下单时，需要先为参数件选择实际采购物料，并核对相应封装和极性；本表没有改动原理图、PCB或固件。
