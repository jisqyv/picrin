/**
 * See Copyright Notice in picrin.h
 */

#include "picrin.h"

#define GET_OPERAND(pic,n) ((pic)->ci->fp[(n)])

struct pic_proc *
pic_get_proc(pic_state *pic)
{
  pic_value v = GET_OPERAND(pic,0);

  if (! pic_proc_p(v)) {
    pic_errorf(pic, "fatal error");
  }
  return pic_proc_ptr(v);
}

/**
 * char type                    desc.
 * ---- ----                    ----
 *  o   pic_value *             object
 *  i   int *                   int
 *  I   int *, bool *           int with exactness
 *  k   size_t *                size_t implicitly converted from int
 *  f   double *                float
 *  F   double *, bool *        float with exactness
 *  s   pic_str **              string object
 *  z   char **                 c string
 *  m   pic_sym **              symbol
 *  v   pic_vec **              vector object
 *  b   pic_blob **             bytevector object
 *  c   char *                  char
 *  l   struct pic_proc **      lambda object
 *  p   struct pic_port **      port object
 *  d   struct pic_dict **      dictionary object
 *  e   struct pic_error **     error object
 *
 *  |                           optional operator
 *  *   size_t *, pic_value **  variable length operator
 */

int
pic_get_args(pic_state *pic, const char *format, ...)
{
  char c;
  size_t paramc, optc, min;
  size_t i , argc = pic->ci->argc - 1;
  va_list ap;
  bool rest = false, opt = false;

  /* paramc: required args count as scheme proc
     optc:   optional args count as scheme proc
     argc:   passed args count as scheme proc
     vargc:  args count passed to this function
  */

  /* check nparams first */
  for (paramc = 0, c = *format; c;  c = format[++paramc]) {
    if (c == '|') {
      opt = true;
      break;
    }
    else if (c == '*') {
      rest = true;
      break;
    }
  }

  for (optc = 0;  opt && c; c = format[paramc + opt + ++optc]) {
     if (c == '*') {
      rest = true;
      break;
    }
  }

  /* '|' should be followed by at least 1 char */
  assert((opt ? 1 : 0) <= optc);
  /* '*' should not be followed by any char */
  assert(format[paramc + opt + optc + rest] == '\0');

  /* check argc. */
  if (argc < paramc || (paramc + optc < argc && ! rest)) {
    pic_errorf(pic, "%s: wrong number of arguments (%d for %s%d)",
               pic_symbol_name(pic, pic_proc_name(pic_proc_ptr(GET_OPERAND(pic, 0)))) ,
               argc,
               rest? "at least " : "",
               paramc);
  }

  /* start dispatching */
  va_start(ap, format);
  min = paramc + optc < argc ? paramc + optc : argc;
  for (i = 1; i < min + 1; i++) {

    c = *format++;
    /* skip '|' if exists. This is always safe because of assert and argc check */
    c = c == '|' ? *format++ : c;

    switch (c) {
    case 'o': {
      pic_value *p;

      p = va_arg(ap, pic_value*);
      *p = GET_OPERAND(pic,i);
      break;
    }
#if PIC_ENABLE_FLOAT
    case 'f': {
      double *f;
      pic_value v;

      f = va_arg(ap, double *);

      v = GET_OPERAND(pic, i);
      switch (pic_type(v)) {
      case PIC_TT_FLOAT:
        *f = pic_float(v);
        break;
      case PIC_TT_INT:
        *f = pic_int(v);
        break;
      default:
        pic_errorf(pic, "pic_get_args: expected float or int, but got ~s", v);
      }
      break;
    }
    case 'F': {
      double *f;
      bool *e;
      pic_value v;

      f = va_arg(ap, double *);
      e = va_arg(ap, bool *);

      v = GET_OPERAND(pic, i);
      switch (pic_type(v)) {
      case PIC_TT_FLOAT:
        *f = pic_float(v);
        *e = false;
        break;
      case PIC_TT_INT:
        *f = pic_int(v);
        *e = true;
        break;
      default:
        pic_errorf(pic, "pic_get_args: expected float or int, but got ~s", v);
      }
      break;
    }
    case 'I': {
      int *k;
      bool *e;
      pic_value v;

      k = va_arg(ap, int *);
      e = va_arg(ap, bool *);

      v = GET_OPERAND(pic, i);
      switch (pic_type(v)) {
      case PIC_TT_FLOAT:
        *k = (int)pic_float(v);
        *e = false;
        break;
      case PIC_TT_INT:
        *k = pic_int(v);
        *e = true;
        break;
      default:
        pic_errorf(pic, "pic_get_args: expected float or int, but got ~s", v);
      }
      break;
    }
#endif
    case 'i': {
      int *k;
      pic_value v;

      k = va_arg(ap, int *);

      v = GET_OPERAND(pic, i);
      switch (pic_type(v)) {
#if PIC_ENABLE_FLOAT
      case PIC_TT_FLOAT:
        *k = (int)pic_float(v);
        break;
#endif
      case PIC_TT_INT:
        *k = pic_int(v);
        break;
      default:
        pic_errorf(pic, "pic_get_args: expected int, but got ~s", v);
      }
      break;
    }
    case 'k': {
      size_t *k;
      pic_value v;
      int x;
      size_t s;

      k = va_arg(ap, size_t *);

      v = GET_OPERAND(pic, i);
      switch (pic_type(v)) {
      case PIC_TT_INT:
        x = pic_int(v);
        if (x < 0) {
          pic_errorf(pic, "pic_get_args: expected non-negative int, but got ~s", v);
        }
        s = (size_t)x;
        if (sizeof(unsigned) > sizeof(size_t)) {
          if (x != (int)s) {
            pic_errorf(pic, "pic_get_args: int unrepresentable with size_t ~s", v);
          }
        }
        *k = (size_t)x;
        break;
      default:
        pic_errorf(pic, "pic_get_args: expected int, but got ~s", v);
      }
      break;
    }
    case 's': {
      pic_str **str;
      pic_value v;

      str = va_arg(ap, pic_str **);
      v = GET_OPERAND(pic,i);
      if (pic_str_p(v)) {
        *str = pic_str_ptr(v);
      }
      else {
        pic_errorf(pic, "pic_get_args: expected string, but got ~s", v);
      }
      break;
    }
    case 'z': {
      const char **cstr;
      pic_value v;

      cstr = va_arg(ap, const char **);
      v = GET_OPERAND(pic,i);
      if (pic_str_p(v)) {
        *cstr = pic_str_cstr(pic, pic_str_ptr(v));
      }
      else {
        pic_errorf(pic, "pic_get_args: expected string, but got ~s", v);
      }
      break;
    }
    case 'm': {
      pic_sym **m;
      pic_value v;

      m = va_arg(ap, pic_sym **);
      v = GET_OPERAND(pic,i);
      if (pic_sym_p(v)) {
        *m = pic_sym_ptr(v);
      }
      else {
        pic_errorf(pic, "pic_get_args: expected symbol, but got ~s", v);
      }
      break;
    }
    case 'v': {
      struct pic_vector **vec;
      pic_value v;

      vec = va_arg(ap, struct pic_vector **);
      v = GET_OPERAND(pic,i);
      if (pic_vec_p(v)) {
        *vec = pic_vec_ptr(v);
      }
      else {
        pic_errorf(pic, "pic_get_args: expected vector, but got ~s", v);
      }
      break;
    }
    case 'b': {
      struct pic_blob **b;
      pic_value v;

      b = va_arg(ap, struct pic_blob **);
      v = GET_OPERAND(pic,i);
      if (pic_blob_p(v)) {
        *b = pic_blob_ptr(v);
      }
      else {
        pic_errorf(pic, "pic_get_args: expected bytevector, but got ~s", v);
      }
      break;
    }
    case 'c': {
      char *k;
      pic_value v;

      k = va_arg(ap, char *);
      v = GET_OPERAND(pic,i);
      if (pic_char_p(v)) {
        *k = pic_char(v);
      }
      else {
        pic_errorf(pic, "pic_get_args: expected char, but got ~s", v);
      }
      break;
    }
    case 'l': {
      struct pic_proc **l;
      pic_value v;

      l = va_arg(ap, struct pic_proc **);
      v = GET_OPERAND(pic,i);
      if (pic_proc_p(v)) {
        *l = pic_proc_ptr(v);
      }
      else {
        pic_errorf(pic, "pic_get_args, expected procedure, but got ~s", v);
      }
      break;
    }
    case 'p': {
      struct pic_port **p;
      pic_value v;

      p = va_arg(ap, struct pic_port **);
      v = GET_OPERAND(pic,i);
      if (pic_port_p(v)) {
        *p = pic_port_ptr(v);
      }
      else {
        pic_errorf(pic, "pic_get_args, expected port, but got ~s", v);
      }
      break;
    }
    case 'd': {
      struct pic_dict **d;
      pic_value v;

      d = va_arg(ap, struct pic_dict **);
      v = GET_OPERAND(pic,i);
      if (pic_dict_p(v)) {
        *d = pic_dict_ptr(v);
      }
      else {
        pic_errorf(pic, "pic_get_args, expected dictionary, but got ~s", v);
      }
      break;
    }
    case 'r': {
      struct pic_record **r;
      pic_value v;

      r = va_arg(ap, struct pic_record **);
      v = GET_OPERAND(pic,i);
      if (pic_record_p(v)) {
        *r = pic_record_ptr(v);
      }
      else {
        pic_errorf(pic, "pic_get_args: expected record, but got ~s", v);
      }
      break;
    }
    case 'e': {
      struct pic_error **e;
      pic_value v;

      e = va_arg(ap, struct pic_error **);
      v = GET_OPERAND(pic,i);
      if (pic_error_p(v)) {
        *e = pic_error_ptr(v);
      }
      else {
        pic_errorf(pic, "pic_get_args, expected error");
      }
      break;
    }
    default:
      pic_errorf(pic, "pic_get_args: invalid argument specifier '%c' given", c);
    }
  }
  if (rest) {
      size_t *n;
      pic_value **argv;

      n = va_arg(ap, size_t *);
      argv = va_arg(ap, pic_value **);
      *n = (size_t)(argc - (i - 1));
      *argv = &GET_OPERAND(pic, i);
  }
  va_end(ap);
  return argc;
}

static void
vm_push_cxt(pic_state *pic)
{
  pic_callinfo *ci = pic->ci;

  ci->cxt = (struct pic_context *)pic_obj_alloc(pic, sizeof(struct pic_context) + sizeof(pic_value) * (size_t)(ci->regc), PIC_TT_CXT);
  ci->cxt->up = ci->up;
  ci->cxt->regc = ci->regc;
  ci->cxt->regs = ci->regs;
}

static void
vm_tear_off(pic_callinfo *ci)
{
  struct pic_context *cxt;
  int i;

  assert(ci->cxt != NULL);

  cxt = ci->cxt;

  if (cxt->regs == cxt->storage) {
    return;                     /* is torn off */
  }
  for (i = 0; i < cxt->regc; ++i) {
    cxt->storage[i] = cxt->regs[i];
  }
  cxt->regs = cxt->storage;
}

void
pic_vm_tear_off(pic_state *pic)
{
  pic_callinfo *ci;

  for (ci = pic->ci; ci > pic->cibase; ci--) {
    if (ci->cxt != NULL) {
      vm_tear_off(ci);
    }
  }
}

static struct pic_irep *
vm_get_irep(pic_state *pic)
{
  pic_value self;
  struct pic_irep *irep;

  self = pic->ci->fp[0];
  if (! pic_proc_p(self)) {
    pic_errorf(pic, "logic flaw");
  }
  irep = pic_proc_ptr(self)->u.i.irep;
  if (! pic_proc_irep_p(pic_proc_ptr(self))) {
    pic_errorf(pic, "logic flaw");
  }
  return irep;
}

#if VM_DEBUG
# define OPCODE_EXEC_HOOK pic_dump_code(c)
#else
# define OPCODE_EXEC_HOOK ((void)0)
#endif

#if PIC_DIRECT_THREADED_VM
# define VM_LOOP JUMP;
# define CASE(x) L_##x: OPCODE_EXEC_HOOK;
# define NEXT pic->ip++; JUMP;
# define JUMP c = *pic->ip; goto *oplabels[c.insn];
# define VM_LOOP_END
#else
# define VM_LOOP for (;;) { c = *pic->ip; switch (c.insn) {
# define CASE(x) case x:
# define NEXT pic->ip++; break
# define JUMP break
# define VM_LOOP_END } }
#endif

#define PUSH(v) (*pic->sp++ = (v))
#define POP() (*--pic->sp)

#define PUSHCI() (++pic->ci)
#define POPCI() (pic->ci--)

#if VM_DEBUG
# define VM_BOOT_PRINT                          \
  do {                                          \
    puts("### booting VM... ###");              \
    stbase = pic->sp;                           \
    cibase = pic->ci;                           \
  } while (0)
#else
# define VM_BOOT_PRINT
#endif

#if VM_DEBUG
# define VM_END_PRINT                                                   \
  do {                                                                  \
    puts("**VM END STATE**");                                           \
    printf("stbase\t= %p\nsp\t= %p\n", (void *)stbase, (void *)pic->sp); \
    printf("cibase\t= %p\nci\t= %p\n", (void *)cibase, (void *)pic->ci); \
    if (stbase < pic->sp - 1) {                                         \
      pic_value *sp;                                                    \
      printf("* stack trace:");                                         \
      for (sp = stbase; pic->sp != sp; ++sp) {                          \
        pic_debug(pic, *sp);                                            \
        puts("");                                                       \
      }                                                                 \
    }                                                                   \
    if (stbase > pic->sp - 1) {                                         \
      puts("*** stack underflow!");                                     \
    }                                                                   \
  } while (0)
#else
# define VM_END_PRINT
#endif

#if VM_DEBUG
# define VM_CALL_PRINT                                                  \
  do {                                                                  \
    short i;                                                            \
    puts("\n== calling proc...");                                       \
    printf("  proc = ");                                                \
    pic_debug(pic, pic_obj_value(proc));                                \
    puts("");                                                           \
    printf("  argv = (");                                               \
    for (i = 1; i < c.u.i; ++i) {                                       \
      if (i > 1)                                                        \
        printf(" ");                                                    \
      pic_debug(pic, pic->sp[-c.u.i + i]);                              \
    }                                                                   \
    puts(")");                                                          \
    if (! pic_proc_func_p(proc)) {                                      \
      printf("  irep = %p\n", proc->u.i.irep);                          \
      printf("  name = %s\n", pic_symbol_name(pic, pic_proc_name(proc))); \
      pic_dump_irep(proc->u.i.irep);                                    \
    }                                                                   \
    else {                                                              \
      printf("  cfunc = %p\n", (void *)proc->u.f.func);                 \
      printf("  name = %s\n", pic_symbol_name(pic, pic_proc_name(proc))); \
    }                                                                   \
    puts("== end\n");                                                   \
  } while (0)
#else
# define VM_CALL_PRINT
#endif

pic_value
pic_apply(pic_state *pic, struct pic_proc *proc, pic_value args)
{
  pic_code c;
  size_t ai = pic_gc_arena_preserve(pic);
  pic_code boot[2];

#if PIC_DIRECT_THREADED_VM
  static const void *oplabels[] = {
    &&L_OP_NOP, &&L_OP_POP, &&L_OP_PUSHUNDEF, &&L_OP_PUSHNIL, &&L_OP_PUSHTRUE,
    &&L_OP_PUSHFALSE, &&L_OP_PUSHINT, &&L_OP_PUSHCHAR, &&L_OP_PUSHCONST,
    &&L_OP_GREF, &&L_OP_GSET, &&L_OP_LREF, &&L_OP_LSET, &&L_OP_CREF, &&L_OP_CSET,
    &&L_OP_JMP, &&L_OP_JMPIF, &&L_OP_NOT, &&L_OP_CALL, &&L_OP_TAILCALL, &&L_OP_RET,
    &&L_OP_LAMBDA, &&L_OP_CONS, &&L_OP_CAR, &&L_OP_CDR, &&L_OP_NILP,
    &&L_OP_SYMBOLP, &&L_OP_PAIRP,
    &&L_OP_ADD, &&L_OP_SUB, &&L_OP_MUL, &&L_OP_DIV, &&L_OP_MINUS,
    &&L_OP_EQ, &&L_OP_LT, &&L_OP_LE, &&L_OP_STOP
  };
#endif

#if VM_DEBUG
  pic_value *stbase;
  pic_callinfo *cibase;
#endif

  if (! pic_list_p(args)) {
    pic_errorf(pic, "argv must be a proper list");
  }
  else {
    int argc, i;

    argc = (int)pic_length(pic, args) + 1;

    VM_BOOT_PRINT;

    PUSH(pic_obj_value(proc));
    for (i = 1; i < argc; ++i) {
      PUSH(pic_car(pic, args));
      args = pic_cdr(pic, args);
    }

    /* boot! */
    boot[0].insn = OP_CALL;
    boot[0].u.i = argc;
    boot[1].insn = OP_STOP;
    pic->ip = boot;
  }

  VM_LOOP {
    CASE(OP_NOP) {
      NEXT;
    }
    CASE(OP_POP) {
      (void)(POP());
      NEXT;
    }
    CASE(OP_PUSHUNDEF) {
      PUSH(pic_undef_value());
      NEXT;
    }
    CASE(OP_PUSHNIL) {
      PUSH(pic_nil_value());
      NEXT;
    }
    CASE(OP_PUSHTRUE) {
      PUSH(pic_true_value());
      NEXT;
    }
    CASE(OP_PUSHFALSE) {
      PUSH(pic_false_value());
      NEXT;
    }
    CASE(OP_PUSHINT) {
      PUSH(pic_int_value(c.u.i));
      NEXT;
    }
    CASE(OP_PUSHCHAR) {
      PUSH(pic_char_value(c.u.c));
      NEXT;
    }
    CASE(OP_PUSHCONST) {
      struct pic_irep *irep = vm_get_irep(pic);

      PUSH(irep->pool[c.u.i]);
      NEXT;
    }
    CASE(OP_GREF) {
      struct pic_irep *irep = vm_get_irep(pic);
      pic_sym *sym;

      sym = irep->syms[c.u.i];
      if (! pic_dict_has(pic, pic->globals, sym)) {
        pic_errorf(pic, "logic flaw; reference to uninitialized global variable: %s", pic_symbol_name(pic, sym));
      }
      PUSH(pic_dict_ref(pic, pic->globals, sym));
      NEXT;
    }
    CASE(OP_GSET) {
      struct pic_irep *irep = vm_get_irep(pic);
      pic_sym *sym;
      pic_value val;

      sym = irep->syms[c.u.i];

      val = POP();
      pic_dict_set(pic, pic->globals, sym, val);
      NEXT;
    }
    CASE(OP_LREF) {
      pic_callinfo *ci = pic->ci;
      struct pic_irep *irep;

      if (ci->cxt != NULL && ci->cxt->regs == ci->cxt->storage) {
        irep = pic_get_proc(pic)->u.i.irep;
        if (c.u.i >= irep->argc + irep->localc) {
          PUSH(ci->cxt->regs[c.u.i - (ci->regs - ci->fp)]);
          NEXT;
        }
      }
      PUSH(pic->ci->fp[c.u.i]);
      NEXT;
    }
    CASE(OP_LSET) {
      pic_callinfo *ci = pic->ci;
      struct pic_irep *irep;

      if (ci->cxt != NULL && ci->cxt->regs == ci->cxt->storage) {
        irep = pic_get_proc(pic)->u.i.irep;
        if (c.u.i >= irep->argc + irep->localc) {
          ci->cxt->regs[c.u.i - (ci->regs - ci->fp)] = POP();
          NEXT;
        }
      }
      pic->ci->fp[c.u.i] = POP();
      NEXT;
    }
    CASE(OP_CREF) {
      int depth = c.u.r.depth;
      struct pic_context *cxt;

      cxt = pic->ci->up;
      while (--depth) {
	cxt = cxt->up;
      }
      PUSH(cxt->regs[c.u.r.idx]);
      NEXT;
    }
    CASE(OP_CSET) {
      int depth = c.u.r.depth;
      struct pic_context *cxt;

      cxt = pic->ci->up;
      while (--depth) {
	cxt = cxt->up;
      }
      cxt->regs[c.u.r.idx] = POP();
      NEXT;
    }
    CASE(OP_JMP) {
      pic->ip += c.u.i;
      JUMP;
    }
    CASE(OP_JMPIF) {
      pic_value v;

      v = POP();
      if (! pic_false_p(v)) {
	pic->ip += c.u.i;
	JUMP;
      }
      NEXT;
    }
    CASE(OP_NOT) {
      pic_value v;

      v = pic_false_p(POP()) ? pic_true_value() : pic_false_value();
      PUSH(v);
      NEXT;
    }
    CASE(OP_CALL) {
      pic_value x, v;
      pic_callinfo *ci;

      if (c.u.i == -1) {
        pic->sp += pic->ci[1].retc - 1;
        c.u.i = pic->ci[1].retc + 1;
      }

    L_CALL:
      x = pic->sp[-c.u.i];
      if (! pic_proc_p(x)) {
	pic_errorf(pic, "invalid application: ~s", x);
      }
      proc = pic_proc_ptr(x);

      VM_CALL_PRINT;

      if (pic->sp >= pic->stend) {
        pic_panic(pic, "VM stack overflow");
      }

      ci = PUSHCI();
      ci->argc = c.u.i;
      ci->retc = 1;
      ci->ip = pic->ip;
      ci->fp = pic->sp - c.u.i;
      ci->cxt = NULL;
      if (pic_proc_func_p(pic_proc_ptr(x))) {

        /* invoke! */
        v = proc->u.f.func(pic);
        pic->sp[0] = v;
        pic->sp += pic->ci->retc;

        pic_gc_arena_restore(pic, ai);
        goto L_RET;
      }
      else {
        struct pic_irep *irep = proc->u.i.irep;
	int i;
	pic_value rest;

	if (ci->argc != irep->argc) {
	  if (! (irep->varg && ci->argc >= irep->argc)) {
            pic_errorf(pic, "wrong number of arguments (%d for %s%d)", ci->argc - 1, (irep->varg ? "at least " : ""), irep->argc - 1);
	  }
	}
	/* prepare rest args */
	if (irep->varg) {
	  rest = pic_nil_value();
	  for (i = 0; i < ci->argc - irep->argc; ++i) {
	    pic_gc_protect(pic, v = POP());
	    rest = pic_cons(pic, v, rest);
	  }
	  PUSH(rest);
	}
	/* prepare local variable area */
	if (irep->localc > 0) {
	  int l = irep->localc;
	  if (irep->varg) {
	    --l;
	  }
	  for (i = 0; i < l; ++i) {
	    PUSH(pic_undef_value());
	  }
	}

	/* prepare cxt */
        if (pic_proc_irep_p(proc)) {
          ci->up = proc->u.i.cxt;
        } else {
          ci->up = NULL;
        }
        ci->regc = irep->capturec;
        ci->regs = ci->fp + irep->argc + irep->localc;

	pic->ip = irep->code;
	pic_gc_arena_restore(pic, ai);
	JUMP;
      }
    }
    CASE(OP_TAILCALL) {
      int i, argc;
      pic_value *argv;
      pic_callinfo *ci;

      if (pic->ci->cxt != NULL) {
        vm_tear_off(pic->ci);
      }

      if (c.u.i == -1) {
        pic->sp += pic->ci[1].retc - 1;
        c.u.i = pic->ci[1].retc + 1;
      }

      argc = c.u.i;
      argv = pic->sp - argc;
      for (i = 0; i < argc; ++i) {
	pic->ci->fp[i] = argv[i];
      }
      ci = POPCI();
      pic->sp = ci->fp + argc;
      pic->ip = ci->ip;

      /* c is not changed */
      goto L_CALL;
    }
    CASE(OP_RET) {
      int i, retc;
      pic_value *retv;
      pic_callinfo *ci;

      if (pic->ci->cxt != NULL) {
        vm_tear_off(pic->ci);
      }

      pic->ci->retc = c.u.i;

    L_RET:
      retc = pic->ci->retc;
      retv = pic->sp - retc;
      if (retc == 0) {
        pic->ci->fp[0] = retv[0]; /* copy at least once */
      }
      for (i = 0; i < retc; ++i) {
        pic->ci->fp[i] = retv[i];
      }
      ci = POPCI();
      pic->sp = ci->fp + 1;     /* advance only one! */
      pic->ip = ci->ip;

      NEXT;
    }
    CASE(OP_LAMBDA) {
      pic_value self;
      struct pic_irep *irep;

      self = pic->ci->fp[0];
      if (! pic_proc_p(self)) {
        pic_errorf(pic, "logic flaw");
      }
      irep = pic_proc_ptr(self)->u.i.irep;
      if (! pic_proc_irep_p(pic_proc_ptr(self))) {
        pic_errorf(pic, "logic flaw");
      }

      if (pic->ci->cxt == NULL) {
        vm_push_cxt(pic);
      }

      proc = pic_make_proc_irep(pic, irep->irep[c.u.i], pic->ci->cxt);
      PUSH(pic_obj_value(proc));
      pic_gc_arena_restore(pic, ai);
      NEXT;
    }
    CASE(OP_CONS) {
      pic_value a, b;
      pic_gc_protect(pic, b = POP());
      pic_gc_protect(pic, a = POP());
      PUSH(pic_cons(pic, a, b));
      pic_gc_arena_restore(pic, ai);
      NEXT;
    }
    CASE(OP_CAR) {
      pic_value p;
      p = POP();
      PUSH(pic_car(pic, p));
      NEXT;
    }
    CASE(OP_CDR) {
      pic_value p;
      p = POP();
      PUSH(pic_cdr(pic, p));
      NEXT;
    }
    CASE(OP_NILP) {
      pic_value p;
      p = POP();
      PUSH(pic_bool_value(pic_nil_p(p)));
      NEXT;
    }

    CASE(OP_SYMBOLP) {
      pic_value p;
      p = POP();
      PUSH(pic_bool_value(pic_sym_p(p)));
      NEXT;
    }

    CASE(OP_PAIRP) {
      pic_value p;
      p = POP();
      PUSH(pic_bool_value(pic_pair_p(p)));
      NEXT;
    }

#define DEFINE_ARITH_OP(opcode, op, guard)			\
    CASE(opcode) {						\
      pic_value a, b;						\
      b = POP();						\
      a = POP();						\
      if (pic_int_p(a) && pic_int_p(b)) {			\
	double f = (double)pic_int(a) op (double)pic_int(b);	\
	if (INT_MIN <= f && f <= INT_MAX && (guard)) {		\
	  PUSH(pic_int_value((int)f));				\
	}							\
	else {							\
	  PUSH(pic_float_value(f));				\
	}							\
      }								\
      else if (pic_float_p(a) && pic_float_p(b)) {		\
	PUSH(pic_float_value(pic_float(a) op pic_float(b)));	\
      }								\
      else if (pic_int_p(a) && pic_float_p(b)) {		\
	PUSH(pic_float_value(pic_int(a) op pic_float(b)));	\
      }								\
      else if (pic_float_p(a) && pic_int_p(b)) {		\
	PUSH(pic_float_value(pic_float(a) op pic_int(b)));	\
      }								\
      else {							\
	pic_errorf(pic, #op " got non-number operands");        \
      }								\
      NEXT;							\
    }

#define DEFINE_ARITH_OP2(opcode, op)                            \
    CASE(opcode) {						\
      pic_value a, b;						\
      b = POP();						\
      a = POP();						\
      if (pic_int_p(a) && pic_int_p(b)) {			\
        PUSH(pic_int_value(pic_int(a) op pic_int(b)));          \
      }								\
      else {							\
	pic_errorf(pic, #op " got non-number operands");        \
      }								\
      NEXT;							\
    }

#if PIC_ENABLE_FLOAT
    DEFINE_ARITH_OP(OP_ADD, +, true);
    DEFINE_ARITH_OP(OP_SUB, -, true);
    DEFINE_ARITH_OP(OP_MUL, *, true);
    DEFINE_ARITH_OP(OP_DIV, /, f == round(f));
#else
    DEFINE_ARITH_OP2(OP_ADD, +);
    DEFINE_ARITH_OP2(OP_SUB, -);
    DEFINE_ARITH_OP2(OP_MUL, *);
    DEFINE_ARITH_OP2(OP_DIV, /);
#endif

    CASE(OP_MINUS) {
      pic_value n;
      n = POP();
      if (pic_int_p(n)) {
	PUSH(pic_int_value(-pic_int(n)));
      }
#if PIC_ENABLE_FLOAT
      else if (pic_float_p(n)) {
	PUSH(pic_float_value(-pic_float(n)));
      }
#endif
      else {
	pic_errorf(pic, "unary - got a non-number operand");
      }
      NEXT;
    }

#define DEFINE_COMP_OP(opcode, op)				\
    CASE(opcode) {						\
      pic_value a, b;						\
      b = POP();						\
      a = POP();						\
      if (pic_int_p(a) && pic_int_p(b)) {			\
	PUSH(pic_bool_value(pic_int(a) op pic_int(b)));		\
      }								\
      else if (pic_float_p(a) && pic_float_p(b)) {		\
	PUSH(pic_bool_value(pic_float(a) op pic_float(b)));	\
      }								\
      else if (pic_int_p(a) && pic_float_p(b)) {		\
	PUSH(pic_bool_value(pic_int(a) op pic_float(b)));	\
      }								\
      else if (pic_float_p(a) && pic_int_p(b)) {		\
	PUSH(pic_bool_value(pic_float(a) op pic_int(b)));	\
      }								\
      else {							\
	pic_errorf(pic, #op " got non-number operands");        \
      }								\
      NEXT;							\
    }

#define DEFINE_COMP_OP2(opcode, op)				\
    CASE(opcode) {						\
      pic_value a, b;						\
      b = POP();						\
      a = POP();						\
      if (pic_int_p(a) && pic_int_p(b)) {			\
	PUSH(pic_bool_value(pic_int(a) op pic_int(b)));		\
      }								\
      else {							\
	pic_errorf(pic, #op " got non-number operands");        \
      }								\
      NEXT;							\
    }

#if PIC_ENABLE_FLOAT
    DEFINE_COMP_OP(OP_EQ, ==);
    DEFINE_COMP_OP(OP_LT, <);
    DEFINE_COMP_OP(OP_LE, <=);
#else
    DEFINE_COMP_OP2(OP_EQ, ==);
    DEFINE_COMP_OP2(OP_LT, <);
    DEFINE_COMP_OP2(OP_LE, <=);
#endif

    CASE(OP_STOP) {

      VM_END_PRINT;

      return pic_gc_protect(pic, POP());
    }
  } VM_LOOP_END;
}

pic_value
pic_apply_trampoline(pic_state *pic, struct pic_proc *proc, pic_value args)
{
  pic_value v, it, *sp;
  pic_callinfo *ci;

  PIC_INIT_CODE_I(pic->iseq[0], OP_NOP, 0);
  PIC_INIT_CODE_I(pic->iseq[1], OP_TAILCALL, -1);

  *pic->sp++ = pic_obj_value(proc);

  sp = pic->sp;
  pic_for_each (v, args, it) {
    *sp++ = v;
  }

  ci = PUSHCI();
  ci->ip = pic->iseq;
  ci->fp = pic->sp;
  ci->retc = (int)pic_length(pic, args);

  if (ci->retc == 0) {
    return pic_undef_value();
  } else {
    return pic_car(pic, args);
  }
}

pic_value
pic_apply0(pic_state *pic, struct pic_proc *proc)
{
  return pic_apply(pic, proc, pic_nil_value());
}

pic_value
pic_apply1(pic_state *pic, struct pic_proc *proc, pic_value arg1)
{
  return pic_apply(pic, proc, pic_list1(pic, arg1));
}

pic_value
pic_apply2(pic_state *pic, struct pic_proc *proc, pic_value arg1, pic_value arg2)
{
  return pic_apply(pic, proc, pic_list2(pic, arg1, arg2));
}

pic_value
pic_apply3(pic_state *pic, struct pic_proc *proc, pic_value arg1, pic_value arg2, pic_value arg3)
{
  return pic_apply(pic, proc, pic_list3(pic, arg1, arg2, arg3));
}

pic_value
pic_apply4(pic_state *pic, struct pic_proc *proc, pic_value arg1, pic_value arg2, pic_value arg3, pic_value arg4)
{
  return pic_apply(pic, proc, pic_list4(pic, arg1, arg2, arg3, arg4));
}

pic_value
pic_apply5(pic_state *pic, struct pic_proc *proc, pic_value arg1, pic_value arg2, pic_value arg3, pic_value arg4, pic_value arg5)
{
  return pic_apply(pic, proc, pic_list5(pic, arg1, arg2, arg3, arg4, arg5));
}

void
pic_define_syntactic_keyword(pic_state *pic, struct pic_env *env, pic_sym *sym, pic_sym *uid)
{
  pic_put_variable(pic, env, pic_obj_value(sym), uid);

  if (pic->lib && pic->lib->env == env) {
    pic_export(pic, sym);
  }
}

void
pic_defun_vm(pic_state *pic, const char *name, pic_sym *uid, pic_func_t func)
{
  struct pic_proc *proc;
  pic_sym *sym;

  proc = pic_make_proc(pic, func, name);

  sym = pic_intern_cstr(pic, name);

  pic_put_variable(pic, pic->lib->env, pic_obj_value(sym), uid);

  pic_dict_set(pic, pic->globals, uid, pic_obj_value(proc));

  pic_export(pic, sym);
}

void
pic_define(pic_state *pic, const char *name, pic_value val)
{
  pic_sym *sym, *uid;

  sym = pic_intern_cstr(pic, name);

  if ((uid = pic_find_variable(pic, pic->lib->env, pic_obj_value(sym))) == NULL) {
    uid = pic_add_variable(pic, pic->lib->env, pic_obj_value(sym));
  } else {
    pic_warnf(pic, "redefining global");
  }

  pic_dict_set(pic, pic->globals, uid, val);

  pic_export(pic, sym);
}

void
pic_defun(pic_state *pic, const char *name, pic_func_t cfunc)
{
  pic_define(pic, name, pic_obj_value(pic_make_proc(pic, cfunc, name)));
}

void
pic_defvar(pic_state *pic, const char *name, pic_value init, struct pic_proc *conv)
{
  pic_define(pic, name, pic_obj_value(pic_make_var(pic, init, conv)));
}

pic_value
pic_ref(pic_state *pic, struct pic_lib *lib, const char *name)
{
  pic_sym *sym, *uid;

  sym = pic_intern_cstr(pic, name);

  if ((uid = pic_find_variable(pic, lib->env, pic_obj_value(sym))) == NULL) {
    pic_errorf(pic, "symbol \"%s\" not defined in library ~s", name, lib->name);
  }

  return pic_dict_ref(pic, pic->globals, uid);
}

void
pic_set(pic_state *pic, struct pic_lib *lib, const char *name, pic_value val)
{
  pic_sym *sym, *uid;

  sym = pic_intern_cstr(pic, name);

  if ((uid = pic_find_variable(pic, lib->env, pic_obj_value(sym))) == NULL) {
    pic_errorf(pic, "symbol \"%s\" not defined in library ~s", name, lib->name);
  }

  pic_dict_set(pic, pic->globals, uid, val);
}

pic_value
pic_funcall(pic_state *pic, struct pic_lib *lib, const char *name, pic_list args)
{
  pic_value proc;

  proc = pic_ref(pic, lib, name);

  pic_assert_type(pic, proc, proc);

  return pic_apply(pic, pic_proc_ptr(proc), args);
}

pic_value
pic_funcall0(pic_state *pic, struct pic_lib *lib, const char *name)
{
  return pic_funcall(pic, lib, name, pic_nil_value());
}

pic_value
pic_funcall1(pic_state *pic, struct pic_lib *lib, const char *name, pic_value arg0)
{
  return pic_funcall(pic, lib, name, pic_list1(pic, arg0));
}

pic_value
pic_funcall2(pic_state *pic, struct pic_lib *lib, const char *name, pic_value arg0, pic_value arg1)
{
  return pic_funcall(pic, lib, name, pic_list2(pic, arg0, arg1));
}

pic_value
pic_funcall3(pic_state *pic, struct pic_lib *lib, const char *name, pic_value arg0, pic_value arg1, pic_value arg2)
{
  return pic_funcall(pic, lib, name, pic_list3(pic, arg0, arg1, arg2));
}
