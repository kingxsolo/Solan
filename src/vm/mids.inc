#define METHODS(X) \
  X(M_LEN, "طول") X(M_SPLIT, "قسم") X(M_TRIM, "نظف") X(M_UPPER, "كبير") X(M_LOWER, "صغير") \
  X(M_STARTS, "يبدأ_ب") X(M_ENDS, "ينتهي_ب") X(M_HAS, "يحتوي") X(M_REPLACE, "ابدل") X(M_SLICE, "جزء") \
  X(M_FIND, "ابحث") X(M_REPEAT, "كرر") X(M_REVERSE, "اعكس") X(M_CHARS, "حروف") X(M_PUSH, "اضف") \
  X(M_INSERT, "ادرج") X(M_REMOVE, "احذف") X(M_POP, "اسحب") X(M_JOIN, "ادمج") X(M_SORT, "رتب") \
  X(M_MAP, "حول") X(M_FILTER, "رشح") X(M_REDUCE, "اطوي") X(M_EACH, "نفذ") X(M_COPY, "انسخ") \
  X(M_CLEAR, "امسح") X(M_SUM, "مجموع") X(M_KEYS, "مفاتيح") X(M_VALUES, "قيم") X(M_ITEMS, "ازواج") \
  X(M_MERGE, "دمج") X(M_FINDF, "جد") X(M_ANY, "اي") X(M_ALL, "كل") X(M_LINES, "سطور") X(M_PADL, "املأ_يسار") \
  X(M_PADR, "املأ_يمين") X(M_FORMAT, "نسق") \
  X(M_UNIQUE, "فريد") X(M_FLATTEN, "سطّح") X(M_FIRST, "اول") X(M_LAST, "اخر") X(M_GET, "احصل") \
  X(M_COUNTVAL, "عد") X(M_MINV, "ادنى") X(M_MAXV, "اعلى") X(M_AVG, "متوسط") X(M_ISEMPTY, "فارغ") \
  X(M_INDEXOF, "موقع") X(M_TAKE, "خذ") X(M_SKIP, "اترك") \
  X(M_CHRAT, "حرف_عند") X(M_ISALNUM, "هل_حرف_بالحرف") X(M_TITLE, "عنوان_حالة") X(M_CAPT, "كبّر_اول") \
  X(M_TRIML, "نظف_يسار") X(M_TRIMR, "نظف_يمين") \
  X(M_SETDEFAULT, "اضبط_افتراضي") X(M_ZIP, "اقترن") X(M_EXTEND, "اضف_الكل") X(M_MERGE2, "دمج_جديد")
#define X_ENUM(id, s) id,
enum { METHODS(X_ENUM) M_COUNT };
#define X_NAME(id, s) s,
static const char *method_names[] = { METHODS(X_NAME) };
