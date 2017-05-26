#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <Arduino.h>
#include "motor.h"
#include "constants.h"
#include "command.h"
#include "single_motor_choreo_queue.h"
#include "multi_motor_choreo_queue.h"


#define MOTOR_INDEX short_buffer[0]
#define AMOUNT short_buffer[1]
#define DURATION short_buffer[2]

Motor* motors[NUMBER_OF_MOTORS];
Linear_Procedural_Command_Queue lpcqs[NUM_LPCQ];
Single_Motor_Choreo_Queue smcqs[NUM_SMCQ];
Multi_Motor_Choreo_Queue mmcq;




/*
  There are several commands that can be issued:

  All values are in binary and represent single or double byte numbers.

  =============================
  |        MOTORS OFF         |
  =============================
  |  0                        |
  =============================


  =============================
  |        MOTORS ON          |
  =============================
  |  1                        |
  =============================


  =============================
  |            LPCQ           |
  =============================
  |  2                        |
  |  <motor>                  |
  |    <amount><duration>     |
  =============================

  =============================
  |           SMCQ            |
  =============================
  |  3                        |
  |    <motor>                |
  |      <num LPCQs>          |
  |      <amount><duration>   |
  |      <amount><duration>   |
  |      <amount><duration>   |
  =============================

  =============================
  |           MMCQ            |
  =============================
  |  4                        |
  |   <num_motors>            |
  |     <motor>               |
  |      <num LPCQs>          |
  |       <amount><duration>  |
  |       <amount><duration>  |
  |       <amount><duration>  |
  |     <motor>               |
  |      <num LPCQs>          |
  |       <amount><duration>  |
  |       <amount><duration>  |
  |       <amount><duration>  |
  =============================

  =============================
  |REMAINING_COMMANDS_ON_MOTOR|
  =============================
  |  5<motor>                 |
  =============================

  =============================
  |        POSITIONS          |
  =============================
  |  6                        |
  =============================
  |  <motor0 position>        |
  |  <motor1 position>        |
  |  <motor2 position>        |
  =============================
 */

int create_lpcq(short *short_buffer, int start_pos){

  char char_buffer[5];
  int  i;
  Serial.readBytes(char_buffer, 4);


  //Amount
  AMOUNT = (unsigned char)char_buffer[0] << 8 | (unsigned char)char_buffer[1];

  //Duration
  DURATION = (unsigned char)char_buffer[2] << 8 | (unsigned char)char_buffer[3];


  //Find an available LPCQ
  for(i=0;i<NUM_LPCQ;i++){
    if(!lpcqs[i].is_active()){
      break;
    }
    //If it didn't break, catch it on the way out
    //and increment to indicate that none are ok
    if(i == NUM_LPCQ -1){
      i++;
    }
  }

  //Are there any lpcqs available and is the command within bounds
  if(i != NUM_LPCQ
     &&
     start_pos + AMOUNT >=
     motors[MOTOR_INDEX]->get_lower_bound()
     &&
     start_pos + AMOUNT <=
     motors[MOTOR_INDEX]->get_upper_bound())
    {
      //Make the LPCQ
      lpcqs[i] = Linear_Procedural_Command_Queue(
                  start_pos,
                  start_pos + AMOUNT,
                  DURATION);


      //Return its index
      return i;

  //Pick out the error
  }else if(i == NUM_LPCQ){
    Serial.println("No LPCQs available");
    return -1;
  }else{
    Serial.println("Dest position is out of bounds");
    Serial.print("Start Pos: ");
    Serial.print(start_pos);
    Serial.print("\n");
    return -2;
  }
}

int create_spcq (short* short_buffer){
  short smcq_index;
  short status;
  short num_lpcqs;
  short i;
  char char_buffer[2];
  Serial.readBytes(char_buffer, 2);
  MOTOR_INDEX = (int)char_buffer[0];
  num_lpcqs = (int)char_buffer[1];

  //Find an available SMCQ
  for(i=0;i<NUM_SMCQ;i++){
    if(!smcqs[i].is_active()){
      break;
    }
    //If it didn't break, catch it on the way out
    //and increment to indicate that none are ok
    if(i == NUM_SMCQ -1){
      i++;
    }
  }

  //If a ununsed SMCQ was found
  if(i != NUM_SMCQ){
    smcq_index = i;
    smcqs[smcq_index]=Single_Motor_Choreo_Queue();

    int pos_buffer;
    pos_buffer = motors[MOTOR_INDEX]->get_pos();
    for(i=0;i<num_lpcqs;i++){
      status = create_lpcq(short_buffer, pos_buffer);
      if(status>=0){
        pos_buffer = pos_buffer + AMOUNT;

        smcqs[smcq_index].insert(&lpcqs[status]);
      }
    }
    return smcq_index;
  }else{
    return -1;
  }
}

void create_mmcq(){
  short short_buffer[5];
  short num_motors;
  char char_buffer[1];
  short i, smcq_index;
  mmcq = Multi_Motor_Choreo_Queue();
  Serial.readBytes(char_buffer, 1);
  num_motors = char_buffer[0];
  for (i=0;i<num_motors;i++){
    smcq_index = create_spcq(short_buffer);
    if(smcq_index>=0){
      mmcq.insert(&smcqs[smcq_index]);
      motors[MOTOR_INDEX]->add_command_queue(&mmcq);
    }
  }
}


void handle_serial_commands
()
{
  byte count;
  char char_buffer[1];
  int  i;

  if(Serial.available() >0){
    count = Serial.readBytes(char_buffer, 1);
    i = char_buffer[0];
    switch(i){
    case(0):
      digitalWrite(MOTOR_SWITCH_PIN, LOW);
      break;
    case(1):
      digitalWrite(MOTOR_SWITCH_PIN, HIGH);
      break;
    case(2):
      {
        short short_buffer[5];
        Serial.readBytes(char_buffer, 1);
        MOTOR_INDEX = (int)char_buffer[0];
        int start_pos = motors[MOTOR_INDEX]->get_pos();
        i = create_lpcq(short_buffer, start_pos);
        //If successfully created an lpcq:
        if(i>=0){
          motors[MOTOR_INDEX]->add_command_queue(&lpcqs[i]);
        }
      }
      break;

    case(3):
      {
        short smcq_index;

        //The short_buffer must remain in this scope,
        //because MOTOR_INDEX is defined to short_buffer[0];
        short short_buffer[5];
        smcq_index = create_spcq(short_buffer);
        if(smcq_index >= 0){
          motors[MOTOR_INDEX]->add_command_queue(&smcqs[smcq_index]);
        }
      }
      break;
    case(4):
      create_mmcq();
      break;
    case(5):
      Serial.println("Remaining commands");
      break;
    case(6):
      Serial.println("Positions");
      for(i=0; i<NUMBER_OF_MOTORS; i++){
        Serial.write(motors[i]->get_pos());
      }
      break;
    default:
      Serial.println("Unkown Command");
      break;

    }
  }
}


#endif
