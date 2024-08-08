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
    : predicate(pred), action1(trueAct), action2(falseAct) {}

  void handleEvent(T& data) noexcept {
    if (predicate(data)) {
      action2();
    } else {
      action1();
    }
  }

private:
  Predicate predicate;
  Action action1;
  Action action2;
};
