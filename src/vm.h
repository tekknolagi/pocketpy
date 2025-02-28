#pragma once

#include "frame.h"
#include "error.h"

namespace pkpy{

#define DEF_NATIVE_2(ctype, ptype)                                      \
    template<> ctype py_cast<ctype>(VM* vm, const PyVar& obj) {         \
        vm->check_type(obj, vm->ptype);                                 \
        return OBJ_GET(ctype, obj);                                     \
    }                                                                   \
    template<> ctype _py_cast<ctype>(VM* vm, const PyVar& obj) {        \
        return OBJ_GET(ctype, obj);                                     \
    }                                                                   \
    template<> ctype& py_cast<ctype&>(VM* vm, const PyVar& obj) {       \
        vm->check_type(obj, vm->ptype);                                 \
        return OBJ_GET(ctype, obj);                                     \
    }                                                                   \
    template<> ctype& _py_cast<ctype&>(VM* vm, const PyVar& obj) {      \
        return OBJ_GET(ctype, obj);                                     \
    }                                                                   \
    PyVar py_var(VM* vm, const ctype& value) { return vm->new_object(vm->ptype, value);}     \
    PyVar py_var(VM* vm, ctype&& value) { return vm->new_object(vm->ptype, std::move(value));}

class Generator: public BaseIter {
    std::unique_ptr<Frame> frame;
    int state; // 0,1,2
public:
    Generator(VM* vm, std::unique_ptr<Frame>&& frame)
        : BaseIter(vm, nullptr), frame(std::move(frame)), state(0) {}

    PyVar next();
};

struct PyTypeInfo{
    PyVar obj;
    Type base;
    Str name;
};

class VM {
    VM* vm;     // self reference for simplify code
public:
    std::stack< std::unique_ptr<Frame> > callstack;
    PyVar _py_op_call;
    PyVar _py_op_yield;
    std::vector<PyTypeInfo> _all_types;

    PyVar run_frame(Frame* frame);

    NameDict _modules;                          // loaded modules
    std::map<StrName, Str> _lazy_modules;       // lazy loaded modules
    PyVar None, True, False, Ellipsis;

    bool use_stdio;
    std::ostream* _stdout;
    std::ostream* _stderr;
    
    PyVar builtins;         // builtins module
    PyVar _main;            // __main__ module

    int recursionlimit = 1000;

    VM(bool use_stdio){
        this->vm = this;
        this->use_stdio = use_stdio;
        if(use_stdio){
            this->_stdout = &std::cout;
            this->_stderr = &std::cerr;
        }else{
            this->_stdout = new StrStream();
            this->_stderr = new StrStream();
        }

        init_builtin_types();
        // for(int i=0; i<128; i++) _ascii_str_pool[i] = new_object(tp_str, std::string(1, (char)i));
    }

    PyVar asStr(const PyVar& obj){
        PyVarOrNull f = getattr(obj, __str__, false, true);
        if(f != nullptr) return call(f);
        return asRepr(obj);
    }

    inline Frame* top_frame() const {
#if PK_EXTRA_CHECK
        if(callstack.empty()) UNREACHABLE();
#endif
        return callstack.top().get();
    }

    PyVar asIter(const PyVar& obj){
        if(is_type(obj, tp_native_iterator)) return obj;
        PyVarOrNull iter_f = getattr(obj, __iter__, false, true);
        if(iter_f != nullptr) return call(iter_f);
        TypeError(OBJ_NAME(_t(obj)).escape(true) + " object is not iterable");
        return nullptr;
    }

    PyVar asList(const PyVar& iterable){
        if(is_type(iterable, tp_list)) return iterable;
        return call(_t(tp_list), one_arg(iterable));
    }

    PyVar* find_name_in_mro(PyObject* cls, StrName name){
        PyVar* val;
        do{
            val = cls->attr().try_get(name);
            if(val != nullptr) return val;
            Type cls_t = static_cast<Py_<Type>*>(cls)->_value;
            Type base = _all_types[cls_t.index].base;
            if(base.index == -1) break;
            cls = _all_types[base.index].obj.get();
        }while(true);
        return nullptr;
    }

    bool isinstance(const PyVar& obj, Type cls_t){
        Type obj_t = OBJ_GET(Type, _t(obj));
        do{
            if(obj_t == cls_t) return true;
            Type base = _all_types[obj_t.index].base;
            if(base.index == -1) break;
            obj_t = base;
        }while(true);
        return false;
    }

    PyVar fast_call(StrName name, Args&& args){
        PyVar* val = find_name_in_mro(_t(args[0]).get(), name);
        if(val != nullptr) return call(*val, std::move(args));
        AttributeError(args[0], name);
        return nullptr;
    }

    inline PyVar call(const PyVar& _callable){
        return call(_callable, no_arg(), no_arg(), false);
    }

    template<typename ArgT>
    inline std::enable_if_t<std::is_same_v<std::decay_t<ArgT>, Args>, PyVar>
    call(const PyVar& _callable, ArgT&& args){
        return call(_callable, std::forward<ArgT>(args), no_arg(), false);
    }

    template<typename ArgT>
    inline std::enable_if_t<std::is_same_v<std::decay_t<ArgT>, Args>, PyVar>
    call(const PyVar& obj, const StrName name, ArgT&& args){
        return call(getattr(obj, name, true, true), std::forward<ArgT>(args), no_arg(), false);
    }

    inline PyVar call(const PyVar& obj, StrName name){
        return call(getattr(obj, name, true, true), no_arg(), no_arg(), false);
    }


    // repl mode is only for setting `frame->id` to 0
    PyVarOrNull exec(Str source, Str filename, CompileMode mode, PyVar _module=nullptr){
        if(_module == nullptr) _module = _main;
        try {
            CodeObject_ code = compile(source, filename, mode);
            return _exec(code, _module);
        }catch (const Exception& e){
            *_stderr << e.summary() << '\n';
        }catch (const std::exception& e) {
            *_stderr << "An std::exception occurred! It could be a bug.\n";
            *_stderr << e.what() << '\n';
        }
        callstack = {};
        return nullptr;
    }

    template<typename ...Args>
    inline std::unique_ptr<Frame> _new_frame(Args&&... args){
        if(callstack.size() > recursionlimit){
            _error("RecursionError", "maximum recursion depth exceeded");
        }
        return std::make_unique<Frame>(std::forward<Args>(args)...);
    }

    template<typename ...Args>
    inline PyVar _exec(Args&&... args){
        callstack.push(_new_frame(std::forward<Args>(args)...));
        return _exec();
    }

    PyVar property(NativeFuncRaw fget){
        PyVar p = builtins->attr("property");
        PyVar method = new_object(tp_native_function, NativeFunc(fget, 1, false));
        return call(p, one_arg(method));
    }

    PyVar new_type_object(PyVar mod, StrName name, Type base){
        PyVar obj = make_sp<PyObject, Py_<Type>>(tp_type, _all_types.size());
        PyTypeInfo info{
            .obj = obj,
            .base = base,
            .name = (mod!=nullptr && mod!=builtins) ? Str(OBJ_NAME(mod)+"."+name.str()): name.str()
        };
        if(mod != nullptr) mod->attr().set(name, obj);
        _all_types.push_back(info);
        return obj;
    }

    Type _new_type_object(StrName name, Type base=0) {
        PyVar obj = new_type_object(nullptr, name, base);
        return OBJ_GET(Type, obj);
    }

    template<typename T>
    inline PyVar new_object(const PyVar& type, const T& _value) {
#if PK_EXTRA_CHECK
        if(!is_type(type, tp_type)) UNREACHABLE();
#endif
        return make_sp<PyObject, Py_<std::decay_t<T>>>(OBJ_GET(Type, type), _value);
    }
    template<typename T>
    inline PyVar new_object(const PyVar& type, T&& _value) {
#if PK_EXTRA_CHECK
        if(!is_type(type, tp_type)) UNREACHABLE();
#endif
        return make_sp<PyObject, Py_<std::decay_t<T>>>(OBJ_GET(Type, type), std::move(_value));
    }

    template<typename T>
    inline PyVar new_object(Type type, const T& _value) {
        return make_sp<PyObject, Py_<std::decay_t<T>>>(type, _value);
    }
    template<typename T>
    inline PyVar new_object(Type type, T&& _value) {
        return make_sp<PyObject, Py_<std::decay_t<T>>>(type, std::move(_value));
    }

    PyVar _find_type(const Str& type){
        PyVar* obj = builtins->attr().try_get(type);
        if(!obj){
            for(auto& t: _all_types) if(t.name == type) return t.obj;
            throw std::runtime_error("type not found: " + type);
        }
        return *obj;
    }

    template<int ARGC>
    void bind_func(Str type, Str name, NativeFuncRaw fn) {
        bind_func<ARGC>(_find_type(type), name, fn);
    }

    template<int ARGC>
    void bind_method(Str type, Str name, NativeFuncRaw fn) {
        bind_method<ARGC>(_find_type(type), name, fn);
    }

    template<int ARGC, typename... Args>
    void bind_static_method(Args&&... args) {
        bind_func<ARGC>(std::forward<Args>(args)...);
    }

    template<int ARGC>
    void _bind_methods(std::vector<Str> types, Str name, NativeFuncRaw fn) {
        for(auto& type: types) bind_method<ARGC>(type, name, fn);
    }

    template<int ARGC>
    void bind_builtin_func(Str name, NativeFuncRaw fn) {
        bind_func<ARGC>(builtins, name, fn);
    }

    int normalized_index(int index, int size){
        if(index < 0) index += size;
        if(index < 0 || index >= size){
            IndexError(std::to_string(index) + " not in [0, " + std::to_string(size) + ")");
        }
        return index;
    }

    // for quick access
    Type tp_object, tp_type, tp_int, tp_float, tp_bool, tp_str;
    Type tp_list, tp_tuple;
    Type tp_function, tp_native_function, tp_native_iterator, tp_bound_method;
    Type tp_slice, tp_range, tp_module, tp_ref;
    Type tp_super, tp_exception, tp_star_wrapper;

    template<typename P>
    inline PyVar PyIter(P&& value) {
        static_assert(std::is_base_of_v<BaseIter, std::decay_t<P>>);
        return new_object(tp_native_iterator, std::forward<P>(value));
    }

    inline BaseIter* PyIter_AS_C(const PyVar& obj)
    {
        check_type(obj, tp_native_iterator);
        return static_cast<BaseIter*>(obj->value());
    }
    
    /***** Error Reporter *****/
    void _error(StrName name, const Str& msg){
        _error(Exception(name, msg));
    }

    void _raise(){
        bool ok = top_frame()->jump_to_exception_handler();
        if(ok) throw HandledException();
        else throw UnhandledException();
    }

public:
    void IOError(const Str& msg) { _error("IOError", msg); }
    void NotImplementedError(){ _error("NotImplementedError", ""); }
    void TypeError(const Str& msg){ _error("TypeError", msg); }
    void ZeroDivisionError(){ _error("ZeroDivisionError", "division by zero"); }
    void IndexError(const Str& msg){ _error("IndexError", msg); }
    void ValueError(const Str& msg){ _error("ValueError", msg); }
    void NameError(StrName name){ _error("NameError", "name " + name.str().escape(true) + " is not defined"); }

    void AttributeError(PyVar obj, StrName name){
        _error("AttributeError", "type " +  OBJ_NAME(_t(obj)).escape(true) + " has no attribute " + name.str().escape(true));
    }

    void AttributeError(Str msg){ _error("AttributeError", msg); }

    inline void check_type(const PyVar& obj, Type type){
        if(is_type(obj, type)) return;
        TypeError("expected " + OBJ_NAME(_t(type)).escape(true) + ", but got " + OBJ_NAME(_t(obj)).escape(true));
    }

    inline PyVar& _t(Type t){
        return _all_types[t.index].obj;
    }

    inline PyVar& _t(const PyVar& obj){
        if(is_int(obj)) return _t(tp_int);
        if(is_float(obj)) return _t(tp_float);
        return _all_types[OBJ_GET(Type, _t(obj->type)).index].obj;
    }

    ~VM() {
        if(!use_stdio){
            delete _stdout;
            delete _stderr;
        }
    }

    inline PyVarOrNull getattr(const PyVar& obj, StrName name, bool throw_err=true, bool class_only=false){
        return getattr(&obj, name, throw_err, class_only);
    }
    template<typename T>
    inline void setattr(PyVar& obj, StrName name, T&& value){
        setattr(&obj, name, std::forward<T>(value));
    }

    CodeObject_ compile(Str source, Str filename, CompileMode mode);
    void post_init();
    PyVar num_negated(const PyVar& obj);
    f64 num_to_float(const PyVar& obj);
    const PyVar& asBool(const PyVar& obj);
    i64 hash(const PyVar& obj);
    PyVar asRepr(const PyVar& obj);
    PyVar new_module(StrName name);
    Str disassemble(CodeObject_ co);
    void init_builtin_types();
    PyVar call(const PyVar& _callable, Args args, const Args& kwargs, bool opCall);
    void unpack_args(Args& args);
    PyVarOrNull getattr(const PyVar* obj, StrName name, bool throw_err=true, bool class_only=false);
    template<typename T>
    void setattr(PyVar* obj, StrName name, T&& value);
    template<int ARGC>
    void bind_method(PyVar obj, Str funcName, NativeFuncRaw fn);
    template<int ARGC>
    void bind_func(PyVar obj, Str funcName, NativeFuncRaw fn);
    void _error(Exception e);
    PyVar _exec();

    template<typename P>
    PyVarRef PyRef(P&& value);
    const BaseRef* PyRef_AS_C(const PyVar& obj);
};

PyVar NativeFunc::operator()(VM* vm, Args& args) const{
    int args_size = args.size() - (int)method;  // remove self
    if(argc != -1 && args_size != argc) {
        vm->TypeError("expected " + std::to_string(argc) + " arguments, but got " + std::to_string(args_size));
    }
    return f(vm, args);
}

void CodeObject::optimize(VM* vm){
    std::vector<StrName> keys;
    for(auto& p: names) if(p.second == NAME_LOCAL) keys.push_back(p.first);
    uint32_t base_n = (uint32_t)(keys.size() / kLocalsLoadFactor + 0.5);
    perfect_locals_capacity = find_next_capacity(base_n);
    perfect_hash_seed = find_perfect_hash_seed(perfect_locals_capacity, keys);

    for(int i=1; i<codes.size(); i++){
        if(codes[i].op == OP_UNARY_NEGATIVE && codes[i-1].op == OP_LOAD_CONST){
            codes[i].op = OP_NO_OP;
            int pos = codes[i-1].arg;
            consts[pos] = vm->num_negated(consts[pos]);
        }

        if(i>=2 && codes[i].op == OP_BUILD_INDEX){
            const Bytecode& a = codes[i-1];
            const Bytecode& x = codes[i-2];
            if(codes[i].arg == 1){
                if(a.op == OP_LOAD_NAME && x.op == OP_LOAD_NAME){
                    codes[i].op = OP_FAST_INDEX;
                }else continue;
            }else{
                if(a.op == OP_LOAD_NAME_REF && x.op == OP_LOAD_NAME_REF){
                    codes[i].op = OP_FAST_INDEX_REF;
                }else continue;
            }
            codes[i].arg = (a.arg << 16) | x.arg;
            codes[i-1].op = OP_NO_OP;
            codes[i-2].op = OP_NO_OP;
        }
    }

    // pre-compute sn in co_consts
    for(int i=0; i<consts.size(); i++){
        if(is_type(consts[i], vm->tp_str)){
            Str& s = OBJ_GET(Str, consts[i]);
            s._cached_sn_index = StrName::get(s.c_str()).index;
        }
    }
}

DEF_NATIVE_2(Str, tp_str)
DEF_NATIVE_2(List, tp_list)
DEF_NATIVE_2(Tuple, tp_tuple)
DEF_NATIVE_2(Function, tp_function)
DEF_NATIVE_2(NativeFunc, tp_native_function)
DEF_NATIVE_2(BoundMethod, tp_bound_method)
DEF_NATIVE_2(Range, tp_range)
DEF_NATIVE_2(Slice, tp_slice)
DEF_NATIVE_2(Exception, tp_exception)
DEF_NATIVE_2(StarWrapper, tp_star_wrapper)

#define PY_CAST_INT(T) \
template<> T py_cast<T>(VM* vm, const PyVar& obj){ \
    vm->check_type(obj, vm->tp_int); \
    return (T)(obj.bits >> 2); \
} \
template<> T _py_cast<T>(VM* vm, const PyVar& obj){ \
    return (T)(obj.bits >> 2); \
}

PY_CAST_INT(char)
PY_CAST_INT(short)
PY_CAST_INT(int)
PY_CAST_INT(long)
PY_CAST_INT(long long)
PY_CAST_INT(unsigned char)
PY_CAST_INT(unsigned short)
PY_CAST_INT(unsigned int)
PY_CAST_INT(unsigned long)
PY_CAST_INT(unsigned long long)


template<> float py_cast<float>(VM* vm, const PyVar& obj){
    vm->check_type(obj, vm->tp_float);
    i64 bits = obj.bits;
    bits = (bits >> 2) << 2;
    return __8B(bits)._float;
}
template<> float _py_cast<float>(VM* vm, const PyVar& obj){
    i64 bits = obj.bits;
    bits = (bits >> 2) << 2;
    return __8B(bits)._float;
}
template<> double py_cast<double>(VM* vm, const PyVar& obj){
    vm->check_type(obj, vm->tp_float);
    i64 bits = obj.bits;
    bits = (bits >> 2) << 2;
    return __8B(bits)._float;
}
template<> double _py_cast<double>(VM* vm, const PyVar& obj){
    i64 bits = obj.bits;
    bits = (bits >> 2) << 2;
    return __8B(bits)._float;
}


#define PY_VAR_INT(T) \
    PyVar py_var(VM* vm, T _val){           \
        i64 val = static_cast<i64>(_val);   \
        if(((val << 2) >> 2) != val){       \
            vm->_error("OverflowError", std::to_string(val) + " is out of range");  \
        }                                                                           \
        val = (val << 2) | 0b01;                                                    \
        return PyVar(reinterpret_cast<int*>(val));                                  \
    }

PY_VAR_INT(char)
PY_VAR_INT(short)
PY_VAR_INT(int)
PY_VAR_INT(long)
PY_VAR_INT(long long)
PY_VAR_INT(unsigned char)
PY_VAR_INT(unsigned short)
PY_VAR_INT(unsigned int)
PY_VAR_INT(unsigned long)
PY_VAR_INT(unsigned long long)

#define PY_VAR_FLOAT(T) \
    PyVar py_var(VM* vm, T _val){           \
        f64 val = static_cast<f64>(_val);   \
        i64 bits = __8B(val)._int;          \
        bits = (bits >> 2) << 2;            \
        bits |= 0b10;                       \
        return PyVar(reinterpret_cast<int*>(bits)); \
    }

PY_VAR_FLOAT(float)
PY_VAR_FLOAT(double)

const PyVar& py_var(VM* vm, bool val){
    return val ? vm->True : vm->False;
}

template<> bool py_cast<bool>(VM* vm, const PyVar& obj){
    vm->check_type(obj, vm->tp_bool);
    return obj == vm->True;
}
template<> bool _py_cast<bool>(VM* vm, const PyVar& obj){
    return obj == vm->True;
}

PyVar py_var(VM* vm, const char val[]){
    return VAR(Str(val));
}

PyVar py_var(VM* vm, std::string val){
    return VAR(Str(std::move(val)));
}

template<typename T>
void _check_py_class(VM* vm, const PyVar& obj){
    vm->check_type(obj, T::_type(vm));
}

PyVar VM::num_negated(const PyVar& obj){
    if (is_int(obj)){
        return VAR(-CAST(i64, obj));
    }else if(is_float(obj)){
        return VAR(-CAST(f64, obj));
    }
    TypeError("expected 'int' or 'float', got " + OBJ_NAME(_t(obj)).escape(true));
    return nullptr;
}

f64 VM::num_to_float(const PyVar& obj){
    if(is_float(obj)){
        return CAST(f64, obj);
    } else if (is_int(obj)){
        return (f64)CAST(i64, obj);
    }
    TypeError("expected 'int' or 'float', got " + OBJ_NAME(_t(obj)).escape(true));
    return 0;
}

const PyVar& VM::asBool(const PyVar& obj){
    if(is_type(obj, tp_bool)) return obj;
    if(obj == None) return False;
    if(is_type(obj, tp_int)) return VAR(CAST(i64, obj) != 0);
    if(is_type(obj, tp_float)) return VAR(CAST(f64, obj) != 0.0);
    PyVarOrNull len_fn = getattr(obj, __len__, false, true);
    if(len_fn != nullptr){
        PyVar ret = call(len_fn);
        return VAR(CAST(i64, ret) > 0);
    }
    return True;
}

i64 VM::hash(const PyVar& obj){
    if (is_type(obj, tp_str)) return CAST(Str&, obj).hash();
    if (is_int(obj)) return CAST(i64, obj);
    if (is_type(obj, tp_tuple)) {
        i64 x = 1000003;
        const Tuple& items = CAST(Tuple&, obj);
        for (int i=0; i<items.size(); i++) {
            i64 y = hash(items[i]);
            x = x ^ (y + 0x9e3779b9 + (x << 6) + (x >> 2)); // recommended by Github Copilot
        }
        return x;
    }
    if (is_type(obj, tp_type)) return obj.bits;
    if (is_type(obj, tp_bool)) return _CAST(bool, obj) ? 1 : 0;
    if (is_float(obj)){
        f64 val = CAST(f64, obj);
        return (i64)std::hash<f64>()(val);
    }
    TypeError("unhashable type: " +  OBJ_NAME(_t(obj)).escape(true));
    return 0;
}

PyVar VM::asRepr(const PyVar& obj){
    return call(obj, __repr__);
}

PyVar VM::new_module(StrName name) {
    PyVar obj = new_object(tp_module, DummyModule());
    obj->attr().set(__name__, VAR(name.str()));
    _modules.set(name, obj);
    return obj;
}

Str VM::disassemble(CodeObject_ co){
    std::vector<int> jumpTargets;
    for(auto byte : co->codes){
        if(byte.op == OP_JUMP_ABSOLUTE || byte.op == OP_SAFE_JUMP_ABSOLUTE || byte.op == OP_POP_JUMP_IF_FALSE){
            jumpTargets.push_back(byte.arg);
        }
    }
    StrStream ss;
    ss << std::string(54, '-') << '\n';
    ss << co->name << ":\n";
    int prev_line = -1;
    for(int i=0; i<co->codes.size(); i++){
        const Bytecode& byte = co->codes[i];
        if(byte.op == OP_NO_OP) continue;
        Str line = std::to_string(byte.line);
        if(byte.line == prev_line) line = "";
        else{
            if(prev_line != -1) ss << "\n";
            prev_line = byte.line;
        }

        std::string pointer;
        if(std::find(jumpTargets.begin(), jumpTargets.end(), i) != jumpTargets.end()){
            pointer = "-> ";
        }else{
            pointer = "   ";
        }
        ss << pad(line, 8) << pointer << pad(std::to_string(i), 3);
        ss << " " << pad(OP_NAMES[byte.op], 20) << " ";
        // ss << pad(byte.arg == -1 ? "" : std::to_string(byte.arg), 5);
        std::string argStr = byte.arg == -1 ? "" : std::to_string(byte.arg);
        if(byte.op == OP_LOAD_CONST){
            argStr += " (" + CAST(Str, asRepr(co->consts[byte.arg])) + ")";
        }
        if(byte.op == OP_LOAD_NAME_REF || byte.op == OP_LOAD_NAME || byte.op == OP_RAISE || byte.op == OP_STORE_NAME){
            argStr += " (" + co->names[byte.arg].first.str().escape(true) + ")";
        }
        if(byte.op == OP_FAST_INDEX || byte.op == OP_FAST_INDEX_REF){
            auto& a = co->names[byte.arg & 0xFFFF];
            auto& x = co->names[(byte.arg >> 16) & 0xFFFF];
            argStr += " (" + a.first.str() + '[' + x.first.str() + "])";
        }
        ss << pad(argStr, 20);      // may overflow
        ss << co->blocks[byte.block].to_string();
        if(i != co->codes.size() - 1) ss << '\n';
    }
    StrStream consts;
    consts << "co_consts: ";
    consts << CAST(Str, asRepr(VAR(co->consts)));

    StrStream names;
    names << "co_names: ";
    List list;
    for(int i=0; i<co->names.size(); i++){
        list.push_back(VAR(co->names[i].first.str()));
    }
    names << CAST(Str, asRepr(VAR(list)));
    ss << '\n' << consts.str() << '\n' << names.str() << '\n';

    for(int i=0; i<co->consts.size(); i++){
        PyVar obj = co->consts[i];
        if(is_type(obj, tp_function)){
            const auto& f = CAST(Function&, obj);
            ss << disassemble(f.code);
        }
    }
    return Str(ss.str());
}

void VM::init_builtin_types(){
    // Py_(Type type, T&& val)
    PyVar _tp_object = make_sp<PyObject, Py_<Type>>(Type(1), Type(0));
    PyVar _tp_type = make_sp<PyObject, Py_<Type>>(Type(1), Type(1));
    _all_types.push_back({.obj = _tp_object, .base = -1, .name = "object"});
    _all_types.push_back({.obj = _tp_type, .base = 0, .name = "type"});
    tp_object = 0; tp_type = 1;

    tp_int = _new_type_object("int");
    tp_float = _new_type_object("float");
    if(tp_int.index != kTpIntIndex || tp_float.index != kTpFloatIndex) UNREACHABLE();

    tp_bool = _new_type_object("bool");
    tp_str = _new_type_object("str");
    tp_list = _new_type_object("list");
    tp_tuple = _new_type_object("tuple");
    tp_slice = _new_type_object("slice");
    tp_range = _new_type_object("range");
    tp_module = _new_type_object("module");
    tp_ref = _new_type_object("_ref");
    tp_star_wrapper = _new_type_object("_star_wrapper");
    
    tp_function = _new_type_object("function");
    tp_native_function = _new_type_object("native_function");
    tp_native_iterator = _new_type_object("native_iterator");
    tp_bound_method = _new_type_object("bound_method");
    tp_super = _new_type_object("super");
    tp_exception = _new_type_object("Exception");

    this->None = new_object(_new_type_object("NoneType"), DUMMY_VAL);
    this->Ellipsis = new_object(_new_type_object("ellipsis"), DUMMY_VAL);
    this->True = new_object(tp_bool, true);
    this->False = new_object(tp_bool, false);
    this->_py_op_call = new_object(_new_type_object("_py_op_call"), DUMMY_VAL);
    this->_py_op_yield = new_object(_new_type_object("_py_op_yield"), DUMMY_VAL);
    this->builtins = new_module("builtins");
    this->_main = new_module("__main__");
    
    // setup public types
    builtins->attr().set("type", _t(tp_type));
    builtins->attr().set("object", _t(tp_object));
    builtins->attr().set("bool", _t(tp_bool));
    builtins->attr().set("int", _t(tp_int));
    builtins->attr().set("float", _t(tp_float));
    builtins->attr().set("str", _t(tp_str));
    builtins->attr().set("list", _t(tp_list));
    builtins->attr().set("tuple", _t(tp_tuple));
    builtins->attr().set("range", _t(tp_range));

    post_init();
    for(int i=0; i<_all_types.size(); i++){
        auto& t = _all_types[i];
        t.obj->attr()._try_perfect_rehash();
    }
    for(auto [k, v]: _modules.items()) v->attr()._try_perfect_rehash();
}

PyVar VM::call(const PyVar& _callable, Args args, const Args& kwargs, bool opCall){
    if(is_type(_callable, tp_type)){
        PyVar* new_f = _callable->attr().try_get(__new__);
        PyVar obj;
        if(new_f != nullptr){
            obj = call(*new_f, std::move(args), kwargs, false);
        }else{
            obj = new_object(_callable, DummyInstance());
            PyVarOrNull init_f = getattr(obj, __init__, false, true);
            if (init_f != nullptr) call(init_f, std::move(args), kwargs, false);
        }
        return obj;
    }

    const PyVar* callable = &_callable;
    if(is_type(*callable, tp_bound_method)){
        auto& bm = CAST(BoundMethod&, *callable);
        callable = &bm.method;      // get unbound method
        args.extend_self(bm.obj);
    }
    
    if(is_type(*callable, tp_native_function)){
        const auto& f = OBJ_GET(NativeFunc, *callable);
        if(kwargs.size() != 0) TypeError("native_function does not accept keyword arguments");
        return f(this, args);
    } else if(is_type(*callable, tp_function)){
        const Function& fn = CAST(Function&, *callable);
        NameDict_ locals = make_sp<NameDict>(
            fn.code->perfect_locals_capacity,
            kLocalsLoadFactor,
            fn.code->perfect_hash_seed
        );

        int i = 0;
        for(StrName name : fn.args){
            if(i < args.size()){
                locals->set(name, std::move(args[i++]));
                continue;
            }
            TypeError("missing positional argument " + name.str().escape(true));
        }

        locals->update(fn.kwargs);

        if(!fn.starred_arg.empty()){
            List vargs;        // handle *args
            while(i < args.size()) vargs.push_back(std::move(args[i++]));
            locals->set(fn.starred_arg, VAR(Tuple::from_list(std::move(vargs))));
        }else{
            for(StrName key : fn.kwargs_order){
                if(i < args.size()){
                    locals->set(key, std::move(args[i++]));
                }else{
                    break;
                }
            }
            if(i < args.size()) TypeError("too many arguments");
        }
        
        for(int i=0; i<kwargs.size(); i+=2){
            const Str& key = CAST(Str&, kwargs[i]);
            if(!fn.kwargs.contains(key)){
                TypeError(key.escape(true) + " is an invalid keyword argument for " + fn.name.str() + "()");
            }
            locals->set(key, kwargs[i+1]);
        }
        const PyVar& _module = fn._module != nullptr ? fn._module : top_frame()->_module;
        auto _frame = _new_frame(fn.code, _module, locals, fn._closure);
        if(fn.code->is_generator) return PyIter(Generator(this, std::move(_frame)));
        callstack.push(std::move(_frame));
        if(opCall) return _py_op_call;
        return _exec();
    }

    PyVarOrNull call_f = getattr(_callable, __call__, false, true);
    if(call_f != nullptr){
        return call(call_f, std::move(args), kwargs, false);
    }
    TypeError(OBJ_NAME(_t(*callable)).escape(true) + " object is not callable");
    return None;
}

void VM::unpack_args(Args& args){
    List unpacked;
    for(int i=0; i<args.size(); i++){
        if(is_type(args[i], tp_star_wrapper)){
            auto& star = _CAST(StarWrapper&, args[i]);
            if(!star.rvalue) UNREACHABLE();
            PyVar list = asList(star.obj);
            List& list_c = CAST(List&, list);
            unpacked.insert(unpacked.end(), list_c.begin(), list_c.end());
        }else{
            unpacked.push_back(args[i]);
        }
    }
    args = Args::from_list(std::move(unpacked));
}

using Super = std::pair<PyVar, Type>;

// https://docs.python.org/3/howto/descriptor.html#invocation-from-an-instance
PyVarOrNull VM::getattr(const PyVar* obj, StrName name, bool throw_err, bool class_only){
    PyObject* objtype = _t(*obj).get();
    if(is_type(*obj, tp_super)){
        const Super& super = OBJ_GET(Super, *obj);
        obj = &super.first;
        objtype = _t(super.second).get();
    }
    PyVar* cls_var = find_name_in_mro(objtype, name);
    if(cls_var != nullptr){
        // handle descriptor
        PyVar* descr_get = _t(*cls_var)->attr().try_get(__get__);
        if(descr_get != nullptr) return call(*descr_get, two_args(*cls_var, *obj));
    }
    // handle instance __dict__
    if(!class_only && !(*obj).is_tagged() && (*obj)->is_attr_valid()){
        PyVar* val = (*obj)->attr().try_get(name);
        if(val != nullptr) return *val;
    }
    if(cls_var != nullptr){
        // bound method is non-data descriptor
        if(is_type(*cls_var, tp_function) || is_type(*cls_var, tp_native_function)){
            return VAR(BoundMethod(*obj, *cls_var));
        }
        return *cls_var;
    }
    if(throw_err) AttributeError(*obj, name);
    return nullptr;
}

template<typename T>
void VM::setattr(PyVar* obj, StrName name, T&& value){
    static_assert(std::is_same_v<std::decay_t<T>, PyVar>);
    PyObject* objtype = _t(*obj).get();
    if(is_type(*obj, tp_super)){
        Super& super = OBJ_GET(Super, *obj);
        obj = &super.first;
        objtype = _t(super.second).get();
    }
    PyVar* cls_var = find_name_in_mro(objtype, name);
    if(cls_var != nullptr){
        // handle descriptor
        const PyVar& cls_var_t = _t(*cls_var);
        if(cls_var_t->attr().contains(__get__)){
            PyVar* descr_set = cls_var_t->attr().try_get(__set__);
            if(descr_set != nullptr){
                call(*descr_set, three_args(*cls_var, *obj, std::forward<T>(value)));
            }else{
                TypeError("readonly attribute: " + name.str().escape(true));
            }
            return;
        }
    }
    // handle instance __dict__
    if((*obj).is_tagged() || !(*obj)->is_attr_valid()) TypeError("cannot set attribute");
    (*obj)->attr().set(name, std::forward<T>(value));
}

template<int ARGC>
void VM::bind_method(PyVar obj, Str name, NativeFuncRaw fn) {
    check_type(obj, tp_type);
    obj->attr().set(name, VAR(NativeFunc(fn, ARGC, true)));
}

template<int ARGC>
void VM::bind_func(PyVar obj, Str name, NativeFuncRaw fn) {
    obj->attr().set(name, VAR(NativeFunc(fn, ARGC, false)));
}

void VM::_error(Exception e){
    if(callstack.empty()){
        e.is_re = false;
        throw e;
    }
    top_frame()->push(VAR(e));
    _raise();
}

PyVar VM::_exec(){
    Frame* frame = top_frame();
    i64 base_id = frame->id;
    PyVar ret = nullptr;
    bool need_raise = false;

    while(true){
        if(frame->id < base_id) UNREACHABLE();
        try{
            if(need_raise){ need_raise = false; _raise(); }
            ret = run_frame(frame);
            if(ret == _py_op_yield) return _py_op_yield;
            if(ret != _py_op_call){
                if(frame->id == base_id){      // [ frameBase<- ]
                    callstack.pop();
                    return ret;
                }else{
                    callstack.pop();
                    frame = callstack.top().get();
                    frame->push(ret);
                }
            }else{
                frame = callstack.top().get();  // [ frameBase, newFrame<- ]
            }
        }catch(HandledException& e){
            continue;
        }catch(UnhandledException& e){
            PyVar obj = frame->pop();
            Exception& _e = CAST(Exception&, obj);
            _e.st_push(frame->snapshot());
            callstack.pop();
            if(callstack.empty()) throw _e;
            frame = callstack.top().get();
            frame->push(obj);
            if(frame->id < base_id) throw ToBeRaisedException();
            need_raise = true;
        }catch(ToBeRaisedException& e){
            need_raise = true;
        }
    }
}

}   // namespace pkpy