//------------Config Relay --------------------
#pragma once

struct Relay{
  unsigned pin_;
  char state_;
};

void OnRelay(Relay& rel){
  rel.state_ = 1;
  digitalWrite(rel.pin_, HIGH);
}


void OffRelay(Relay& rel){
  rel.state_ = 0;
  digitalWrite(rel.pin_, LOW);
}

void SwapRelay(Relay& rel){
  if(rel.state_ == 0){
    OnRelay(rel);
  }
    
  else{
    OffRelay(rel);
  }
}

char isRelayOn(Relay& rel){
  return rel.state_;
}
//--------------------------------