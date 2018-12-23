/*
 * File:   main.c
 * Author: zetsutenman
 *
 * Created on 2018/12/22, 17:23
 */

#include <xc.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#pragma config FOSC = INTOSC    // Oscillator Selection (INTOSC oscillator: I/O function on CLKIN pin)
#pragma config WDTE = OFF       // Watchdog Timer Enable (WDT disabled)
#pragma config PWRTE = ON       // Power-up Timer Enable (PWRT enabled)
#pragma config MCLRE = OFF      // MCLR Pin Function Select (MCLR/VPP pin function is MCLR)
#pragma config CP = OFF         // Flash Program Memory Code Protection (Program memory code protection is disabled)
#pragma config CPD = OFF        // Data Memory Code Protection (Data memory code protection is disabled)
#pragma config BOREN = OFF      // Brown-out Reset Enable (Brown-out Reset disabled)
#pragma config CLKOUTEN = OFF   // Clock Out Enable (CLKOUT function is disabled. I/O or oscillator function on the CLKOUT pin)
#pragma config IESO = OFF       // Internal/External Switchover (Internal/External Switchover mode is disabled)
#pragma config FCMEN = OFF      // Fail-Safe Clock Monitor Enable (Fail-Safe Clock Monitor is disabled)

#pragma config WRT = OFF        // Flash Memory Self-Write Protection (Write protection off)
#pragma config PLLEN = ON       // PLL Enable (4x PLL enabled)
#pragma config STVREN = ON      // Stack Overflow/Underflow Reset Enable (Stack Overflow or Underflow will cause a Reset)
#pragma config BORV = HI        // Brown-out Reset Voltage Selection (Brown-out Reset Voltage (Vbor), high trip point selected.)
#pragma config LVP = OFF        // Low-Voltage Programming Enable (High-voltage on MCLR/VPP must be used for programming)

#define _XTAL_FREQ          32000000

unsigned int count = 0;

// 設定したPWM周期毎にカウントアップ
void interrupt interCountPWMperiod() {
    if(TMR2IF == 1) {
        count++;
        TMR2IF = 0;
    }
}

/*
 * RA5でセンサ出力を読み取り、デバイス固有コードと受信したコードが一致したらRA2を1秒間だけHigh出力に設定
 */
void main() {

    // PIC設定
    OSCCON = 0b01110000;    // 内部クロック8MHz（PLLEN=ONなので32MHzで動作）
    ANSELA = 0b00000000;    // すべてデジタルI/Oに割当
    TRISA  = 0b00101000;    // RA3とRA5は入力、それ以外はすべて出力
    PORTA  = 0b00000000;    // すべてのピンの出力をLowとする
    WPUA   = 0b00001000;    // 内部プルアップ抵抗はRA3のみ有効
    
    T2CON   = 0b00000000;   // TMR2プリスケーラ値を1倍に設定
    TMR2    = 0;            // Timer2カウンターを初期化
    PR2     = 210;          // PWM周期を約38kHzで設定
    TMR2IF  = 0;            // Timer2割込フラグ初期化
    
    TMR2IE = 1;             // Timer2割込を許可
    PEIE   = 1;             // 周辺装置の割込を許可
    GIE    = 1;             // 全割込を許可
    
    long code = 0x02FD48B7;         // デバイス固有コード。カスタマーコード16bit+データコード16bit（REGZAの電源ボタン）
    signed char bit_index = 31;     // 固有コードのどのビットを見ているか
    unsigned char bit_num = 0;      // 固有コードから取り出した各ビットの値
    bool flag_sensor_enb = false;
    bool flag_onoff = true;
    bool flag_leader = true;
    bool flag_code = true;
    bool flag_bit_read = true;
    
    // NECフォーマットに準拠
    // PWM周期P=26.375μsec  
    // 変調単位T=562μsec=21.3P
    // リーダーコード　ON→16T=341P OFF→8T=171P
    // デバイス固有コード　0：ON→1T=22P、OFF→1T=20P　1：ON→1T=22P、OFF→3T=64P
    
    while(true) {
        // 赤外線センサ入力監視
        if(!flag_sensor_enb) {
            if(TMR2ON == 0 && RA5 == 0) { // 赤外線センサが信号を受信
                flag_sensor_enb = true;
                TMR2ON = 1;
            }else if(TMR2ON == 1) { // 受信の成功・失敗にかかわらず終了
                // 変数の再初期化
                flag_sensor_enb = false;
                flag_onoff = true;
                flag_leader = true;
                flag_code = true;
                flag_bit_read = true;
                bit_index = 31;
                TMR2ON = 0;
                count = 0;
                TMR2 = 0;
            }
        }else{ // 赤外線センサが信号を受信中
            if(flag_leader) { // リーダーコード受信中
                if(flag_onoff && RA5 == 1) { // リーダーコードON期間終了
                    if(count > 323 && count < 359) { // リーダーコードのON期間か判定（±10Pの誤差は許容）
                        flag_onoff = false;
                        count = 0;
                    }else{
                        flag_sensor_enb = false; // 受信失敗
                    }
                }else if(!flag_onoff && RA5 == 0) { // リーダーコードOFF期間終了
                    if(count > 162 && count < 180) { // リーダーコードのOFF期間か判定（±10Pの誤差は許容）
                        flag_onoff = true;
                        flag_leader = false;
                        count = 0;
                    }else{
                        flag_sensor_enb = false; // 受信失敗
                    }
                }
            }else if(flag_code) { // デバイス固有コード受信中
                if(flag_bit_read) { // デバイス固有コードの各ビットの値を取り出す
                    if(bit_index >= 0) {
                        bit_num = (unsigned char)((code >> bit_index) & 1); // 右シフト演算とAND演算で固有コードの最上位ビットから最下位ビットに向かって順番に値を取り出す
                        bit_index--;
                        flag_bit_read = false;
                    }else{ // 受信したコードとデバイスに設定したコードが一致
                        RA2 = 1;
                        __delay_ms(1000);
                        RA2 = 0;
                        flag_sensor_enb = false; // 受信成功
                    }
                }else{ // 取り出したビットの値に対応するOFF期間の長さか判定する
                    if(flag_onoff && RA5 == 1) { // ON期間終了
                        flag_onoff = false;
                        count = 0;
                    }else if(!flag_onoff && RA5 == 0) { // OFF期間終了
                        if(bit_num == 0) { // ビットの値が0
                            if(count > 14 && count < 26) { // OFF期間の判定（±5Pの誤差は許容）
                                flag_onoff = true;
                                flag_bit_read = true;
                                count = 0;
                            }else{
                                flag_sensor_enb = false; // 受信失敗
                            }
                        }else{ // ビットの値が1
                            if(count > 58 && count < 70) { // OFF期間の判定（±5Pの誤差は許容）
                                flag_onoff = true;
                                flag_bit_read = true;
                                count = 0;
                            }else{
                                flag_sensor_enb = false; // 受信失敗
                            }
                        }
                    }
                }
            }
        }
    }
    
    return;
}