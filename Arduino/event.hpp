//------------Config Event--------------------
struct SensorData {
  float temperature;
  float humidity;
};

template<typename T>
class Event {
public:
  typedef int (*Predicate)(const T&);
  typedef void (*Action)();

  Event(Predicate pred, Action trueAct, Action falseAct)
    : predicate(pred), actionTrue(trueAct), actionFalse(falseAct) {}

  void handleEvent(T& data) {
    if (predicate(data)) {
      actionFalse();
    } else {
      actionTrue();
    }
  }

private:
  Predicate predicate;
  Action actionTrue;
  Action actionFalse;
};
//--------------------------------