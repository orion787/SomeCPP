//------------Config Relay --------------------
#pragma once

struct Relay{
  unsigned pin_;
  char state_;
};

void OnRelay(Relay& rel) noexcept{
  rel.state_ = 1;
  digitalWrite(rel.pin_, HIGH);
}


void OffRelay(Relay& rel) noexcept{
  rel.state_ = 0;
  digitalWrite(rel.pin_, LOW);
}

void SwapRelay(Relay& rel) noexcept{
  if(rel.state_ == 0){
    OnRelay(rel);
  }
    
  else{
    OffRelay(rel);
  }
}

char isRelayOn(Relay& rel) noexcept{
  return rel.state_;
}
