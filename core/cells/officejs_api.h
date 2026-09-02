// Internal: register Office.js host natives + bootstrap on a QuickJS context.

#ifndef CELLS_OFFICEJS_API_H_
#define CELLS_OFFICEJS_API_H_

struct JSContext;

namespace cells {

class JsSandbox;

void registerOfficeJsHost(JSContext* ctx, JsSandbox* sandbox);

}  // namespace cells

#endif  // CELLS_OFFICEJS_API_H_
