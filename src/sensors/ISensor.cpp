#include "ISensor.h"

// ISensor 是纯接口类，无独立实现逻辑。
// 具体方法在各子类中实现。

namespace Awareness {
// 确保 ISensor 的 vtable 在此编译单元生成
// （基类有虚析构函数，在某些编译器下需要此处生成 vtable）
} // namespace Awareness
