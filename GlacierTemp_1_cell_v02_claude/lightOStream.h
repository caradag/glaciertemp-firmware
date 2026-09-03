#include <Arduino.h>

#define NL '\n'
#define PRINT '\xA0'
#define NOSPACER '\xAD'
#define DATETIME '\xAE'
#define NORMALTEXT '\xAF'
// Base of the decimals control range: DECIMALS+n places an implied decimal
// point n digits from the right of the next number streamed. The literal
// '\xB0'..'\xBF' forms are used throughout the sketch and mean the same thing.
#define DECIMALS '\xB0'
#define PAD '\xC0'
#define ASTERISK_BAR '\xD0'

long int10Pow(byte power);

class lightOStream {
  char* _buf;
  size_t _size;
  size_t _pos;

  // Formatting options
  int _padWidth = 0;
  char _padChar = ' ';
  bool _forceSign = false;
  uint8_t _decimals = 0;
  char _sign = 0;
  char _separator = ' ';
  bool _verbose = true;

public:
  lightOStream(char* buf, size_t size) : _buf(buf), _size(size), _pos(0) {
    clear();
  }

  // Reset buffer
  void clear() {
    _pos = 0;
    _buf[0] = '\0';
  }

  void setVerbose(bool verboseState) {
    _verbose=verboseState;
  }

  bool getVerbose() {
    return _verbose;
  }

  void resetPadding() {
    _padWidth = 0;
    _separator = ' ';
    _padChar = ' ';
  }

  lightOStream& checkEndOfLine() {
    // Prostprocess funtion after adding data to the buffer
    // we add a spacer at the end of the new data, unless it ends with "(" or a space
    // we print data (and remove any traing spacer) if the last characer is \n
    if(_pos==0){
      // Nothing buffered. Without this, _buf[_pos-1] reads _buf[-1], which
      // happens if a zero-length string is ever streamed on an empty buffer.
      return *this;
    }
    if(_buf[_pos-1]=='\n'){
      if(_pos>=2 && _buf[_pos-2]==_separator){
        // Removing separator at the end of the line
        _pos-=2;
        addChar('\n');      
      }
      resetPadding();
      direct(_buf);    
      clear();
    }else if(_separator!='\0'){
      // Adding a separator if last character is not a spave or a (
      if(_buf[_pos-1]!='(' && _buf[_pos-1]!=' '){
        addChar(_separator);
      }
    }
    return *this;
  }  

  // Add char
  bool addChar(char c) {
    if (c && _pos + 1 < _size) {
      if(c=='\t'){
        _buf[_pos++] = ' ';
        _buf[_pos++] = ' ';
      }else{
        _buf[_pos++] = c;
      }
       _buf[_pos] = '\0';
      return true;
    }
    return false;
  }

  lightOStream& operator<<(char c) {
    if(c>='\xB0' && c<'\xC0'){// \xBX value defines the number of decimals to use
      _decimals = c-0xB0;
    }else if(c=='+'){
      _forceSign = true;
    }else if(c==NOSPACER){
      _separator = '\0';
    }else if(c==DATETIME){
      _padWidth = 2;
      _padChar = '0';
      _separator = '\0';    
    }else if(c==NORMALTEXT){
      resetPadding();
    }else if(c==PRINT){
      resetPadding();
      direct(_buf);    
      clear();      
    }else if(c>=PAD && c<'\xD0'){
      _padWidth = c-PAD;
    }else if(c>=ASTERISK_BAR && c<'\xE0'){
      for(int i=0; i<=(c-ASTERISK_BAR)*10; i++){
        addChar('*');
      }
    }else{
      addChar(c);
      checkEndOfLine();
    }
    return *this;
  }

  void addCharArray(const char* str) {
   while (addChar(*str++));   
  }

  void direct(const char* str) {
    if(_verbose){
      Serial.write(str,strlen(str));
    }     
  }
  // Add string
  lightOStream& operator<<(const char* str) {
    addCharArray(str);
    return checkEndOfLine();
  }

  // Add Flash string helper
  lightOStream& operator<<(const __FlashStringHelper* fstr) {
    PGM_P p = reinterpret_cast<PGM_P>(fstr);
    while (addChar(pgm_read_byte(p++)));
    return checkEndOfLine();
  }


  // Format and add integer
  lightOStream& operator<<(unsigned long val) {
    if (_decimals>0){
      unsigned long left = val/int10Pow(_decimals);
      unsigned long right = val - (left*int10Pow(_decimals));
      ulongNoDecimals(left, _padWidth-(_decimals+1), _padChar);
      addChar('.');
      ulongNoDecimals(right, _decimals, '0');
      _decimals = 0;
    }else{
      ulongNoDecimals(val, _padWidth, _padChar);
    }
    return checkEndOfLine();
  }

void ulongNoDecimals(unsigned long val, int toPad, char toPadWidth) {
      char temp[14];
      ultoa(val,temp,10);
      addPaddingAndSign(strlen(temp), toPad, toPadWidth);
      addCharArray(temp);
  }

void addPaddingAndSign(int strLen, int toPad, char toPadWidth){
      while (strLen < toPad-(_sign!=0)) {
        addChar(toPadWidth);
        strLen++;
      }
      if(_sign!='\0'){
        addChar(_sign);
      }
      _sign='\0';
}

  // Format and add integer
  lightOStream& operator<<(long val) {
    // If forcing sign or negative
    if (val < 0) {
      _sign='-';
      val*=-1;
    }else if(_forceSign){
      _sign='+';
    } 
    _forceSign = false;
    return (*this) << (unsigned long)val;
  }

  // Format and add integer
  lightOStream& operator<<(unsigned int val) {
    return (*this) << (unsigned long)val;
  }  

  lightOStream& operator<<(int val) {
    // If forcing sign or negative
    return (*this) << (long)val;
  }

  // Format and add float
  lightOStream& operator<<(float val) {
    return (*this) << (long)(val*int10Pow(_decimals));
  }

  lightOStream& operator<<(double val) {
    return (*this) << (float)val;
  }  
};
