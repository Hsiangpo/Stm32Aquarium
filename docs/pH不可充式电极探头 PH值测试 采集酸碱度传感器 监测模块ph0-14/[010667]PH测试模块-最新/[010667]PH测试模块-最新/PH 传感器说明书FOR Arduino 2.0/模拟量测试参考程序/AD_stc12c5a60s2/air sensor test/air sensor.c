/******************************************************************** 
                                        龙戈电子 
实现功能:最小系统版气体传感器测试程序 
使用芯片：STC12C5A60S2 
晶振：11.0592MHZ 
波特率：9600 
编译环境：Keil 
作者：LOGO  
网站：www.auto-ctrl.com 
【声明】此程序仅用于学习与参考，引用请注明版权和作者信息！       
*********************************************************************/ 
/******************************************************************** 
说明：1、测试程序开始前会先对气体传感器进行加热
      2、AOUT为模拟电压输出口 1602上A为10位AD转换值  V为对应的模拟电压值  
      3、 当气体浓度大于设定值时 传感器DOUT脚输出低电平 蜂鸣器响
*********************************************************************/ 
#include <reg51.h>
#include <intrins.h>

#define uchar unsigned char
#define uint  unsigned int 

typedef unsigned char BYTE;
typedef bit BOOL;  

sfr   AUXR1     =   0xA2;
sfr   ADC_CONTR     =   0xBC;              //ADC control register
sfr   ADC_RES       =   0xBD;              //ADC high 8-bit result register
sfr	  ADC_RESl	 =	   0xBE;
sfr   P1ASF         =   0x9D;              //P1 secondary function control register

#define   FOSC   18432000L
#define   T1MS   (65536-FOSC/12/2000)      //1ms timer calculation method in 12T mode


#define   ADC_POWER     0x80               //ADC power control bit
#define   ADC_FLAG      0x10               //ADC complete flag
#define   ADC_START    0x08               //ADC start control bit
#define   ADC_SPEEDLL   0x00               //540 clocks
#define   ADC_SPEEDL    0x20               //360 clocks
#define   ADC_SPEEDH   0x40               //180 clocks
#define   ADC_SPEEDHH   0x60               //90 clocks

int i,j,display=0,t02s;
double c;
int cdisplay,low2;
int  wendufazhi=50;
int count,tt=30; 
sbit LCD_RS = P2^5;             
sbit LCD_RW = P2^6;
sbit LCD_EP = P2^7;
sbit warning = P3^2;
sbit beep = P3^4;
//////////////显示数组
BYTE code dis1[] = {"   welcom  to   "};
BYTE code dis2[] = {"3w.auto-ctrl.com"};
BYTE code dis3[] = {"TEST...   "};
BYTE code dis5[] = {"www.auto-ctrl.com"};
BYTE code Dangerous[] = {"Dangerous    "};
BYTE code Safe[] = {"Safe           "};
BYTE code Heat[] = {"Heat.....      "};
BYTE code Wait[] = {"Wait "};

uchar   tab2[]={'v','o','l','t','a','g','e'};

void InitADC();
void ADC();
void flash(); 
unsigned int GetADCResult(int ch);
void ADC();
void delay(int ms)
{                           // 延时子程序
	while(ms--)
	{
	  for(i = 0; i< 250; i++)
	  {
	   _nop_();
	   _nop_();
	   _nop_();
	   _nop_();
	  }
	}
}

BOOL lcd_bz()
{                          // 测试LCD忙碌状态
BOOL result;
LCD_RS = 0;
LCD_RW = 1;
LCD_EP = 1;
_nop_();
_nop_();
_nop_();
_nop_();
result = (BOOL)(P0 & 0x80);
LCD_EP = 0;
return result; 
}

void lcd_wcmd(BYTE cmd)
{                          // 写入指令数据到LCD
while(lcd_bz());
LCD_RS = 0;
LCD_RW = 0;
LCD_EP = 0;
_nop_();
_nop_(); 
P0 = cmd;
_nop_();
_nop_();
_nop_();
_nop_();
LCD_EP = 1;
_nop_();
_nop_();
_nop_();
_nop_();
LCD_EP = 0;  
}

void lcd_pos(BYTE pos)
{                          //设定显示位置
lcd_wcmd(pos | 0x80);
}

void lcd_wdat(BYTE dat) 
{                          //写入字符显示数据到LCD
while(lcd_bz());
LCD_RS = 1;
LCD_RW = 0;
LCD_EP = 0;
P0 = dat;
_nop_();
_nop_();
_nop_();
_nop_();
LCD_EP = 1;
_nop_();
_nop_();
_nop_();
_nop_();
LCD_EP = 0; 
}

void lcd_init()
{                        //LCD初始化设定
lcd_wcmd(0x38);          //16*2显示，5*7点阵，8位数据
delay(1);
lcd_wcmd(0x0c);          //显示开，关光标
delay(1);
lcd_wcmd(0x06);          //移动光标
delay(1);
lcd_wcmd(0x01);          //清除LCD的显示内容
delay(1);
}

main()
{
      BYTE i;
	  int j=0;
      lcd_init();               // 初始化LCD
      delay(10);
     lcd_wcmd(0x06);            //向右移动光标
	 InitADC();
	  TMOD   = 0x01;                      //set timer0 as mode1 (16-bit)
      TL0   = T1MS;                       //initial timer0 low byte
      TH0   = T1MS >> 8;                  //initial timer0 high byte
    while(1)              
   { 
	  switch(display) 
      { 
	    case 0:  
		{	   
	       i=0;
		   while(dis2[ i ] != '\0')
		   {
		   	  lcd_pos(0x80+i);
			  lcd_wdat(dis2[i]);
			  i++;
			  delay(300);		
		   }
	  	  flash();
		  lcd_wcmd(0x01);            //清除LCD的显示内容
          delay(20);                //控制两屏转换时间
		  display = 1;		
		  lcd_wcmd(0x06);            //向右移动光标
		}
		break; 
	    case 1:  
		{
		  delay(300);
	      i = 0;                                                               
	     while(dis1[i] != '\0')                                                
	     {                                      //显示字符"         "   
	       
		    lcd_pos(0x8A+i);               //设置显示位置为第一行第17列            
           lcd_wdat(dis1[i]);                                                  
	       i++;                                                                
	     } 
		 delay(300);
		i = 0;
         while(dis5[i] != '\0')                                                
	     {   
		   lcd_pos(0x4f+i);               //设置显示位置为第一行第17列                                             //显示字符"         "   
	       lcd_wdat(dis5[i]);                                                  
	       i++;                                                                
	     }   
		 delay(300);                                                       
	     for(j=0;j<16;j++)           //向左移动16格                           
	     {                                                                     
	       lcd_wcmd(0x18);           //字符同时左移一格                        
	       delay(800);                   //控制移动时间                        
	     }                                                                     
	         display=2;  
		}
		break;

	    case 2:  
		{	   
	  	  flash();
		  delay(1000);
		  lcd_wcmd(0x01);            //清除LCD的显示内容
          delay(20);                //控制两屏转换时间
		  display = 3;		
		  lcd_wcmd(0x06);            //向右移动光标
		} 
		break;
	   case 3:  
		{
		    i=0;
			while(Heat[ i ] != '\0')
			{
			   lcd_pos(0x80+i);
			   lcd_wdat(Heat[i]);
	           i++;
	           delay(30);
			}
			i=0;
			while(Wait[ i ] != '\0')
			{
			   lcd_pos(0x40+i);
			   lcd_wdat(Wait[i]);
	           i++;
	           delay(30);
			}
			lcd_pos(0x47);
			lcd_wdat('S');
			delay(30);
			display = 4;
		}
		break;
	    case 4:  
		{
		   	   
			   lcd_pos(0x45);
			   lcd_wdat(tt/10+0x30);
			   delay(10);                
		   	   lcd_pos(0x46);
			   lcd_wdat(tt%10+0x30);
			   delay(10);                

			   TR0   = 1;                           //timer0 start running
               ET0   = 1;                           //enable timer0 interrupt
               EA   = 1;                            //open global interrupt switch
			   if(tt==0)
			   {
			   	   TR0   = 0;                           //timer0 start running
                   ET0   = 0;                           //enable timer0 interrupt
                   EA   = 0;                            //open global interrupt switch
				  lcd_wcmd(0x01);            //清除LCD的显示内容
                  delay(10);                //控制两屏转换时间
		          display = 5;		
		          lcd_wcmd(0x06);            //向右移动光标

			   }
			   
  
		}
		break;
		case 5:  
		{	   
			i=0;
			while(dis3[ i ] != '\0')
			{
			   lcd_pos(0x80+i);
			   lcd_wdat(dis3[i]);
	           i++;
	           delay(30);
			}
			display=6;	
		} 
		break;

		case 6:  
		{	   
             
			 if(warning==0)		 //传感器输出低电平
			 {
				if(warning==0)
				{
					i=0;
					while(Dangerous[ i ] != '\0')
					{
					   lcd_pos(0x88+i);
					   lcd_wdat(Dangerous[i]);
			           i++;
			           delay(30);
					}
					 for(i=0;i<3;i++)
					 {
						 beep = 0;
						delay(200);
						beep = 1;
						delay(200);	
					}		
			    }
			 }
		//////////////////////////////////////////
		     if(warning==1)
			 {
				if(warning==1)
				{
					i=0;
					while(Safe[ i ] != '\0')
					{
					   lcd_pos(0x88+i);
					   lcd_wdat(Safe[i]);
			           i++;
			           delay(30);
					}
			    }
			 }	
		     ADC();
		} 
		break;

		default:
		break;
      }

   }
}

void flash()                                                               
{                                                                          
      for(i=0;i<12;i++) 
	   {
      	delay(600);                     //控制停留时间  
	   }                       
        lcd_wcmd(0x08);            //关闭显示                                  
      for(i=0;i<12;i++) 
	  {
		delay(200);                 
	  }                                  
	   lcd_wcmd(0x0c);                                             
      for(i=0;i<12;i++) 
	  {
	   delay(200); 
      }                                                          
      lcd_wcmd(0x08);                                            
      for(i=0;i<12;i++) 
	  {
	   delay(200);                  
      }                                
      lcd_wcmd(0x0c);                                                
      for(i=0;i<12;i++) 
	  {
	    delay(200); 
      }                                                           
} 
void tm0_isr() interrupt 1 using 1
{
      TL0 = T1MS;                         //reload timer0 low byte
      TH0 = T1MS >> 8;                    //reload timer0 high byte
      if (count-- == 0)                   //1ms * 1000 -> 1s
      {
           count = 1000; 
		   tt--;
		   beep = 0;
		   delay(200);
		   beep=1;                              //reset counter
      }
}
 
void ADC()
{
	 int shu=0,shiwei=0,ya=0;    

	 shiwei=GetADCResult(0);
	   lcd_pos(0x40);                //设置显示位置为第一行的第1个字符
	   lcd_wdat('A');
	   lcd_pos(0x41);                
	   lcd_wdat('=');	  
	   lcd_pos(0x42);                
	   lcd_wdat(shiwei/1000+0x30);	  
	   lcd_pos(0x43);                
	   lcd_wdat((shiwei%1000)/100+0x30);
	   lcd_pos(0x44);                
	   lcd_wdat((shiwei%100)/10+0x30);
	   lcd_pos(0x45);                
	   lcd_wdat((shiwei%10)+0x30);
	   c = ((double)shiwei*(5.50/1024))*100;
	   cdisplay =  (int)c;
	  
	   lcd_pos(0x48);                
	   lcd_wdat('V');
	   lcd_pos(0x49);                
	   lcd_wdat('=');
	   lcd_pos(0x4a);                
	   lcd_wdat(cdisplay/100+0x30);
	   lcd_pos(0x4b);                
	   lcd_wdat('.');
	   lcd_pos(0x4c);                
	   lcd_wdat((cdisplay%100)/10+0x30);
	   lcd_pos(0x4e);                
	   lcd_wdat(cdisplay%10+0x30);
	   lcd_pos(0x4f); 
	   lcd_wdat('V');
	   delay(10);
	   
}

/*----------------------------
Get ADC result
----------------------------*/
unsigned int GetADCResult(int ch)
{
      ADC_CONTR = ADC_POWER | ADC_SPEEDLL | ch | ADC_START;
      _nop_();                            //Must wait before inquiry
      _nop_();
      _nop_();
      _nop_();
      while (!(ADC_CONTR & ADC_FLAG));  //Wait complete flag
      ADC_CONTR &= ~ADC_FLAG;           //Close ADC
	  return (ADC_RES*4+ADC_RESl);     //Return ADC result
}
/*----------------------------
Initial ADC sfr
----------------------------*/
void InitADC()
{
      P1ASF = 0xff;                       //Open 8 channels ADC function
	  ADC_RES = 0;
	  ADC_RESl = 0;                       //Clear previous result
      ADC_CONTR = ADC_POWER | ADC_SPEEDLL;
      delay(2);                           //ADC power-on and delay
}