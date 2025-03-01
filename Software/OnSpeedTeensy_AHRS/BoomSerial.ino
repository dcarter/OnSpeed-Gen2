void readBoomSerial()
{
  if (readBoom)
  {
    int packetCount=0;
    // look for serial data from boom
    while (Serial1.available()>0 && packetCount < BOOM_PACKET_SIZE)
    {      
    boomMaxAvailable=max(Serial1.available(),boomMaxAvailable);
    String parseArray[4];
    int parseArrayIndex=0;  
    serialBoomChar = Serial1.read();
    #ifdef BOOMDATADEBUG
    Serial.print(serialBoomChar);
    #endif
    packetCount++;
    lastReceivedBoomTime=millis();
    if (boomBufferString.length()>=50) boomBufferString=""; // prevent serial buffer overflow                 
    if ((boomBufferString.length()>0 || serialBoomChar=='$'))  // if empty accept only $ character (start of boom data line)
      {    
       if (serialBoomChar == char(0x0A))
          {            
              // we have the full line, verify and parse the data               
              // verify if line starting with $
                if (boomBufferString[0]=='$' && boomBufferString.length()>=21) // actual data starts at 21
                {
                boomTimestamp=millis();
                  
#ifdef NOBOOMCHECKSUM
                  // no CRC
                      for (unsigned int k=21;k<boomBufferString.length()-1;k++)
                          {                    
                          if (boomBufferString[k]==',') parseArrayIndex++; else parseArray[parseArrayIndex]+=boomBufferString[k];
                          if (parseArrayIndex>=4) break;
                          } // for                                 
                      boomStatic=BOOM_STATIC_CALC(parseArray[0].toInt());
                      boomDynamic=BOOM_DYNAMIC_CALC(parseArray[1].toInt());                    
                      //if (boomDynamic*100/1.225*2>0) boomIAS=sqrt(boomDynamic*100/1.225*2)* 1.94384; else 
                      boomIAS=0;
                      boomAlpha=BOOM_ALPHA_CALC(parseArray[2].toInt());
                      boomBeta=BOOM_BETA_CALC(parseArray[3].toInt());
#else
                  
                 //calculate CRC
                int calcCRC=0;
                for (unsigned int i=0;i<boomBufferString.length()-4;i++)
                    {                  
                    calcCRC+=boomBufferString[i];
                    }
                calcCRC=calcCRC & 0xFF;
                if (calcCRC==(int)strtol(&boomBufferString.substring(boomBufferString.length()-3, boomBufferString.length()-1)[0],NULL,16)) // convert from hex back into integer for camparison
                    {                
                    // parse data
                    for (unsigned int k=21;k<boomBufferString.length()-4;k++)
                          {                    
                          if (boomBufferString[k]==',') parseArrayIndex++; else parseArray[parseArrayIndex]+=boomBufferString[k];
                          if (parseArrayIndex>=4) break;
                          } // for                                                       
                      boomStatic=BOOM_STATIC_CALC(parseArray[0].toInt());
                      boomDynamic=BOOM_DYNAMIC_CALC(parseArray[1].toInt());                    
                      //if (boomDynamic*100/1.225*2>0) boomIAS=sqrt(boomDynamic*100/1.225*2)* 1.94384; else 
                      boomIAS=0;
                      boomAlpha=BOOM_ALPHA_CALC(parseArray[2].toInt());
                      boomBeta=BOOM_BETA_CALC(parseArray[3].toInt());   
                      
                     } else 
                          {
                          #ifdef BOOMDATADEBUG
                          Serial.println("BOOM: Bad CRC");
                          #endif  
                          // bad CRC    
                          }
#endif                            

#ifdef BOOMDATADEBUG
                      Serial.printf("BOOM: boomStatic %.2f, boomDynamic %.2f, boomAlpha %.2f, boomBeta %.2f, boomIAS %.2f\n", boomStatic, boomDynamic, boomAlpha, boomBeta, boomIAS);
                      //Serial.printf("BOOM: boomStatic %i, boomDynamic %i, boomAlpha %i, boomBeta %i,\n", parseArray[0].toInt(), parseArray[1].toInt(), parseArray[2].toInt(), parseArray[3].toInt());
#endif 
                      
               } // if (boomBufferString[0]='$')
              
              // drop buffer after parsing
              boomBufferString="";
              break; // got packet, exit loop
              } else boomBufferString+=serialBoomChar;            
              
        } // if (boomBufferString.length()>0)
            #ifdef BOOMDATADEBUG
            else Serial.print(serialBoomChar); // display # for dumped characters
            #endif
    } // serial1.available
   } // if BOOM
}
 
