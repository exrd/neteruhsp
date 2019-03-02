
#pragma once

/*=============================================================================

	ライセンス：
		NYSL Version 0.9982
		A. 本ソフトウェアは Everyone'sWare です。このソフトを手にした一人一人が、
		   ご自分の作ったものを扱うのと同じように、自由に利用することが出来ます。
		  A-1. フリーウェアです。作者からは使用料等を要求しません。
		  A-2. 有料無料や媒体の如何を問わず、自由に転載・再配布できます。
		  A-3. いかなる種類の 改変・他プログラムでの利用 を行っても構いません。
		  A-4. 変更したものや部分的に使用したものは、あなたのものになります。
			   公開する場合は、あなたの名前の下で行って下さい。
		B. このソフトを利用することによって生じた損害等について、作者は
		   責任を負わないものとします。各自の責任においてご利用下さい。
		C. 著作者人格権は exrd に帰属します。著作権は放棄します。
		D. 以上の３項は、ソース・実行バイナリの双方に適用されます。

	in English:
		A. This software is "Everyone'sWare". It means:
		Anybody who has this software can use it as if he/she is
		the author.

		A-1. Freeware. No fee is required.
		A-2. You can freely redistribute this software.
		A-3. You can freely modify this software. And the source
		may be used in any software with no limitation.
		A-4. When you release a modified version to public, you
		must publish it with your name.

		B. The author is not responsible for any kind of damages or loss
		while using or misusing this software, which is distributed
		"AS IS". No warranty of any kind is expressed or implied.
		You use AT YOUR OWN RISK.

		C. Copyrighted to exrd

		D. Above three clauses are applied both to source and binary
		form of this software.

============================================================================*/

//=============================================================================
// コンフィグ

// メモリリーク発見用ユーティリティを有効化
#define NHSP_CONFIG_MEMLEAK_DETECTION			(0)

// ベンチマーク用の命令を有効化
#define NHSP_CONFIG_PERFORMANCE_TIMER			(0)

// value_t のメモリアロケーションのキャッシュ
#define NHSP_CONFIG_VALUE_ALLOCATION_CACHE		(1)


//=============================================================================
// ソースコードこっから
#include <cstddef>

namespace neteruhsp
{

//=============================================================================
// 全体
void initialize_system();
void uninitialize_system();

//=============================================================================
// メモリ
void* xmalloc( size_t size );
void  xfree( void* ptr );
void* xrealloc( void* ptr, size_t size );

//=============================================================================
// 文字列バッファ
struct string_buffer_t
{
	char*			buffer_;
	int				buffer_size_;
	int				cursor_;
	int				expand_step_;
};

string_buffer_t* create_string_buffer( size_t initial_len = 1024, int expand_step = -1 );
void destroy_string_buffer( string_buffer_t* sb );

void initialize_string_buffer( string_buffer_t* sb, size_t initial_len = 1024, int expand_step = 1024 );
void uninitialize_string_buffer( string_buffer_t* sb );

void string_buffer_append( string_buffer_t* sb, const char* s, int len = -1 );

//=============================================================================
// リスト
struct list_node_t
{
	list_node_t*	prev_;
	list_node_t*	next_;

	void*			value_;
};

list_node_t* create_list_node();
void destroy_list_node( list_node_t *node );

void link_next( list_node_t* node, list_node_t* list );
void link_prev( list_node_t* node, list_node_t* list );
bool unlink_list_node( list_node_t* node );

struct list_t
{
	list_node_t*	head_;
	list_node_t*	tail_;
};

list_t* create_list();
void destroy_list( list_t* list );

void list_prepend( list_t& list, list_node_t* node );
void list_append( list_t& list, list_node_t* node );
void list_erase( list_t& list, list_node_t* node );
list_node_t* list_find( list_t& list, void* value );
void list_free_all( list_t& list );

//=============================================================================
// キーワード
enum keyword_tag
{
	KEYWORD_UNDEF =-1,

	KEYWORD_GLOBAL =0,
	KEYWORD_CTYPE,
	KEYWORD_END,
	KEYWORD_RETURN,
	KEYWORD_GOTO,
	KEYWORD_GOSUB,
	KEYWORD_REPEAT,
	KEYWORD_LOOP,
	KEYWORD_CONTINUE,
	KEYWORD_BREAK,
	KEYWORD_IF,
	KEYWORD_ELSE,

	MAX_KEYWORD,
};

int query_keyword( const char* s );

//=============================================================================
// トークナイザ
enum token_tag
{
	TOKEN_UNKNOWN =-1,

	TOKEN_EOF =0,
	TOKEN_EOL,
	TOKEN_EOS,

	TOKEN_PP_ARG_INDICATOR,

	TOKEN_LBRACE,
	TOKEN_RBRACE,
	TOKEN_LPARENTHESIS,
	TOKEN_RPARENTHESIS,
	TOKEN_COMMA,

	TOKEN_INTEGER,
	TOKEN_REAL,
	TOKEN_STRING,

	TOKEN_OP_BOR,
	TOKEN_OP_BAND,
	TOKEN_OP_BXOR,
	TOKEN_OP_EQ,
	TOKEN_OP_NEQ,
	TOKEN_OP_GT,
	TOKEN_OP_GTOE,
	TOKEN_OP_LT,
	TOKEN_OP_LTOE,

	TOKEN_OP_ADD,
	TOKEN_OP_SUB,
	TOKEN_OP_MUL,
	TOKEN_OP_DIV,
	TOKEN_OP_MOD,

	TOKEN_ASSIGN,
	TOKEN_ADD_ASSIGN,
	TOKEN_SUB_ASSIGN,
	TOKEN_MUL_ASSIGN,
	TOKEN_DIV_ASSIGN,
	TOKEN_MOD_ASSIGN,
	TOKEN_BOR_ASSIGN,
	TOKEN_BAND_ASSIGN,
	TOKEN_BXOR_ASSIGN,

	TOKEN_IDENTIFIER,

	MAX_TOKEN,
};

struct token_t
{
	token_tag		tag_;
	char*			content_;

	int				cursor_begin_, cursor_end_;
	int				appear_line_;
	bool			left_space_, right_space_;
};

struct tokenize_context_t
{
	const char*		script_;

	int				cursor_;
	int				line_;
	const char*		line_head_;
};

int query_token_shadow( const char* ident, size_t len );

void initialize_tokenize_context( tokenize_context_t* c, const char* script );
void uninitialize_tokenize_context( tokenize_context_t* c );

token_t* get_token( tokenize_context_t& c );
void destroy_token( token_t* t );

char* create_token_string( const char* str, size_t len );

//=============================================================================
// パーサ
struct parse_context_t
{
	list_t*					token_list_;
	list_node_t*			token_current_;
	tokenize_context_t*		tokenize_context_;
};

parse_context_t* create_parse_context();
void destroy_parse_context( parse_context_t* p );

void initialize_parse_context( parse_context_t* c, tokenize_context_t& t );
void uninitialize_parse_context( parse_context_t* c );

token_t* read_token( parse_context_t& c );
void unread_token( parse_context_t& c, size_t num =1 );
token_t* prev_token( parse_context_t& c, size_t num =0 );

//=============================================================================
// プリプロセッサ
enum preprocessor_tag
{
	PREPRO_UNKNOWN =-1,

	PREPRO_DEFINE =0,
	PREPRO_UNDEF,
	PREPRO_IF,
	PREPRO_IFDEF,
	PREPRO_ENDIF,
	PREPRO_ENUM,

	MAX_PREPRO,
};

int query_preprocessor( const char* s );

static const size_t PP_DIRECTIVE_MAX = 16;

struct pp_region_t
{
	bool				is_valid_;
	int					line_;
};

static const size_t MACRO_PARAM_MAX = 16;

struct macro_param_t
{
	char*				default_param_;
};

struct macro_arg_t
{
	char*				arg_param_;
};

void initialize_macro_arg( macro_arg_t* ma );
void uninitialize_macro_arg( macro_arg_t* ma );

struct macro_t
{
	char*				name_;

	bool				is_ctype_;

	char*				replacing_;

	int					param_num_;
	macro_param_t		params_[MACRO_PARAM_MAX];
};

macro_t* create_macro();
void destroy_macro( macro_t* m );

void macro_free_content( macro_t* macro );

struct prepro_context_t
{
	list_t*				macro_list_;

	string_buffer_t*	out_buffer_;

	int					line_;

	bool				is_current_region_valid_;
	int					pp_region_idx_;
	pp_region_t			pp_region_[PP_DIRECTIVE_MAX];

	int					enum_next_;
};

prepro_context_t* create_prepro_context();
void destroy_prepro_context( prepro_context_t* pctx );

void prepro_register_default_macros( prepro_context_t* pctx );

char* prepro_do( const char* src );

char* prepro_line( prepro_context_t* pctx, const char* line, bool enable_preprocessor );
char* prepro_line_expand( prepro_context_t* pctx, const char* line, bool* out_is_replaced = nullptr );

macro_t* prepro_find_macro( prepro_context_t* pctx, const char* name );
bool prepro_register_macro( prepro_context_t* pctx, macro_t* macro );
bool prepro_erase_macro( prepro_context_t* pctx, const char* name );

//=============================================================================
// 抽象構文木
enum node_tag
{
	NODE_EMPTY =0,

	NODE_LABEL,

	NODE_BLOCK_STATEMENTS,

	NODE_COMMAND,
	NODE_ARGUMENTS,

	NODE_ASSIGN,
	NODE_ADD_ASSIGN,
	NODE_SUB_ASSIGN,
	NODE_MUL_ASSIGN,
	NODE_DIV_ASSIGN,
	NODE_MOD_ASSIGN,
	NODE_BOR_ASSIGN,
	NODE_BAND_ASSIGN,
	NODE_BXOR_ASSIGN,
	NODE_VARIABLE,

	NODE_EXPRESSION,
	NODE_BOR,
	NODE_BAND,
	NODE_BXOR,
	NODE_EQ,
	NODE_NEQ,
	NODE_GT,
	NODE_GTOE,
	NODE_LT,
	NODE_LTOE,
	NODE_ADD,
	NODE_SUB,
	NODE_MUL,
	NODE_DIV,
	NODE_MOD,
	NODE_UNARY_MINUS,
	NODE_PRIMITIVE_VALUE,
	NODE_IDENTIFIER_EXPR,

	NODE_END,
	NODE_RETURN,

	NODE_GOTO,
	NODE_GOSUB,

	NODE_REPEAT,
	NODE_LOOP,
	NODE_CONTINUE,
	NODE_BREAK,

	NODE_IF,
	NODE_IF_DISPATCHER,

	MAX_NODE,
};

enum ast_node_flag_tag
{
};

struct ast_node_t
{
	node_tag		tag_;
	token_t*		token_;
	ast_node_t		*left_, *right_;
	unsigned int	flag_;
};

ast_node_t* create_ast_node( node_tag tag, ast_node_t* left =nullptr, ast_node_t* right =nullptr );
ast_node_t* create_ast_node( node_tag tag, token_t* token, ast_node_t* left =nullptr );
void destroy_ast_node( ast_node_t* node );

bool is_eos_like_token( token_tag tag );

list_t* parse_script( parse_context_t& c );
void destroy_ast( list_t* ast );

ast_node_t* parse_statement( parse_context_t& c );

ast_node_t* parse_label_safe( parse_context_t& c );

ast_node_t* parse_control_safe( parse_context_t& c );

ast_node_t* parse_command_safe( parse_context_t& c );
ast_node_t* parse_arguments( parse_context_t& c );

ast_node_t* parse_assign_safe( parse_context_t& c );
ast_node_t* parse_variable_safe( parse_context_t& c );

ast_node_t* parse_expression( parse_context_t& c );
ast_node_t* parse_borand( parse_context_t& c );
ast_node_t* parse_eqneq( parse_context_t& c );
ast_node_t* parse_gtlt( parse_context_t& c );
ast_node_t* parse_addsub( parse_context_t& c );
ast_node_t* parse_muldivmod( parse_context_t& c );
ast_node_t* parse_term( parse_context_t& c );
ast_node_t* parse_primitive( parse_context_t& c );
ast_node_t* parse_identifier_expression( parse_context_t& c );

//=============================================================================
// 変数
enum value_tag
{
	VALUE_NONE,
	VALUE_INT,
	VALUE_DOUBLE,
	VALUE_STRING,
	VALUE_VARIABLE,
};

struct value_t;

struct variable_t
{
	char*			name_;
	value_tag		type_;
	int				granule_size_;
	int				length_;
	void*			data_;
	int				data_size_;
};

variable_t* create_variable( const char* name );
void destroy_variable( variable_t* v );
void prepare_variable( variable_t* v, value_tag type, int granule_size, int length );

list_t* create_variable_table();
void destroy_variable_table( list_t* table );

variable_t* search_variable( list_t* table, const char* name );
void variable_set( list_t* table, const value_t& v, const char* name, int idx );
void variable_set( variable_t* var, const value_t& v, int idx );
void variable_add( variable_t* var, const value_t& v, int idx );
void variable_sub( variable_t* var, const value_t& v, int idx );
void variable_mul( variable_t* var, const value_t& v, int idx );
void variable_div( variable_t* var, const value_t& v, int idx );
void variable_mod( variable_t* var, const value_t& v, int idx );
void variable_bor( variable_t* var, const value_t& v, int idx );
void variable_band( variable_t* var, const value_t& v, int idx );
void variable_bxor( variable_t* var, const value_t& v, int idx );

void* variable_data_ptr( const variable_t& v, int idx );
int variable_calc_int( const variable_t& r, int idx );
double variable_calc_double( const variable_t& r, int idx );
const char* variable_get_string( const variable_t& r, int idx );
char* variable_calc_string( const variable_t& r, int idx );

//=============================================================================
// 値（即値）
struct value_t
{
	value_tag				type_;
	union
	{
		int					ivalue_;
		double				dvalue_;
		char*				svalue_;

		struct
		{
			variable_t*		variable_;
			int				index_;
		};
	};
};

value_t* create_value( int v );
value_t* create_value( double v );
value_t* create_value( const char* v );
value_t* create_value( variable_t* v, int idx );
value_t* create_value( const value_t& v );
value_t* create_value_move( char* v );
void destroy_value( value_t* t );

void value_set( value_t* v, int i );
void value_set( value_t* v, double d );
void value_set( value_t* v, const char* s );

void value_move( value_t* v, char* s );
void value_move( value_t* to, value_t* from );

value_tag value_get_primitive_tag( const value_t& r );
bool value_calc_boolean( const value_t& r );
int value_calc_int( const value_t& r );
double value_calc_double( const value_t& r );
const char* value_get_string( const value_t& r );
char* value_calc_string( const value_t& r );
value_t* value_convert_type( value_tag to, const value_t& r );
void value_isolate( value_t& v );

void value_bor( value_t* v, const value_t& r );
void value_band( value_t* v, const value_t& r );
void value_bxor( value_t* v, const value_t& r );
void value_eq( value_t* v, const value_t& r );
void value_neq( value_t* v, const value_t& r );
void value_gt( value_t* v, const value_t& r );
void value_gtoe( value_t* v, const value_t& r );
void value_lt( value_t* v, const value_t& r );
void value_ltoe( value_t* v, const value_t& r );
void value_add( value_t* v, const value_t& r );
void value_sub( value_t* v, const value_t& r );
void value_mul( value_t* v, const value_t& r );
void value_div( value_t* v, const value_t& r );
void value_mod( value_t* v, const value_t& r );
void value_unary_minus( value_t* v );

//=============================================================================
// スタック
struct value_stack_t
{
	value_t**		stack_;
	int				top_;
	int				max_;
};

value_stack_t* create_value_stack();
void destroy_value_stack( value_stack_t* st );

void initialize_value_stack( value_stack_t* st );
void uninitialize_value_stack( value_stack_t* st );

void stack_push( value_stack_t* st, value_t* v );
void stack_push( value_stack_t* st, const value_t& v );
value_t* stack_peek( value_stack_t* st, int i =-1 );
void stack_pop( value_stack_t* st, size_t n =1 );

//=============================================================================
// システム変数
enum sysvar_tag
{
	SYSVAR_CNT,
	SYSVAR_STAT,
	SYSVAR_REFDVAL,
	SYSVAR_REFSTR,
	SYSVAR_STRSIZE,
	SYSVAR_LOOPLEV,

	MAX_SYSVAR,
};

int query_sysvar( const char* s );

//=============================================================================
// 実行コード
struct label_node_t
{
	char*			name_;
	int				position_;
};

using code_t =int;

enum code_oprator_tag
{
	OPERATOR_NOP =0,

	OPERATOR_PUSH_INT,
	OPERATOR_PUSH_DOUBLE,
	OPERATOR_PUSH_STRING,
	OPERATOR_PUSH_VARIABLE,
	OPERATOR_PUSH_SYSVAR,

	OPERATOR_ASSIGN,
	OPERATOR_ADD_ASSIGN,
	OPERATOR_SUB_ASSIGN,
	OPERATOR_MUL_ASSIGN,
	OPERATOR_DIV_ASSIGN,
	OPERATOR_MOD_ASSIGN,
	OPERATOR_BOR_ASSIGN,
	OPERATOR_BAND_ASSIGN,
	OPERATOR_BXOR_ASSIGN,

	OPERATOR_BOR,
	OPERATOR_BAND,
	OPERATOR_BXOR,

	OPERATOR_EQ,
	OPERATOR_NEQ,
	OPERATOR_GT,
	OPERATOR_GTOE,
	OPERATOR_LT,
	OPERATOR_LTOE,

	OPERATOR_ADD,
	OPERATOR_SUB,
	OPERATOR_MUL,
	OPERATOR_DIV,
	OPERATOR_MOD,

	OPERATOR_UNARY_MINUS,

	OPERATOR_IF,

	OPERATOR_REPEAT,
	OPERATOR_REPEAT_CHECK,
	OPERATOR_LOOP,
	OPERATOR_CONTINUE,
	OPERATOR_BREAK,

	OPERATOR_LABEL,

	OPERATOR_GOSUB,
	OPERATOR_GOTO,

	OPERATOR_COMMAND,
	OPERATOR_FUNCTION,

	OPERATOR_JUMP,
	OPERATOR_JUMP_RELATIVE,
	OPERATOR_RETURN,
	OPERATOR_END,

	MAX_OPERATOR,
};

struct code_container_t
{
	code_t*			code_;
	size_t			code_size_;
	size_t			code_buffer_size_;
};

code_container_t* create_code_container();
void destroy_code_container( code_container_t* c );

//=============================================================================
// 実行環境
struct call_frame_t
{
	int				caller_poisition_;
};
static const size_t MAX_CALL_FRAME = 16;

struct loop_frame_t
{
	int				start_position_;
	int				end_position_;
	int				counter_;
	int				max_;
	int				cnt_;
};
static const size_t MAX_LOOP_FRAME = 16;

struct execute_environment_t
{
	list_t*				parser_list_;
	list_t*				ast_list_;

	list_t*				label_table_;
	list_t*				variable_table_;

	code_container_t*	execute_code_;
};

struct execute_status_t
{
	value_stack_t*	stack_;
	int				pc_;

	call_frame_t	call_frame_[MAX_CALL_FRAME];
	int				current_call_frame_;

	loop_frame_t	loop_frame_[MAX_LOOP_FRAME];
	int				current_loop_frame_;

	bool			is_end_;
	int				stat_;
	double			refdval_;
	char*			refstr_;
	int				strsize_;
};

struct load_arg_t
{
	bool			dump_preprocessed_;
	bool			dump_ast_;
};

execute_environment_t* create_execute_environment();
void destroy_execute_environment( execute_environment_t* e );

void initialize_execute_status( execute_status_t* s );
void uninitialize_execute_status( execute_status_t* s );

void load_script( execute_environment_t* e, const char* script, const load_arg_t* arg =nullptr );
void execute_inner( execute_environment_t* e, execute_status_t* s );
void execute( execute_environment_t* e, int initial_pc =0 );

void generate_and_append_code( execute_environment_t* e, list_t* ast );

value_t* evaluate_ast_immediate( ast_node_t* ast );
bool evaluate_ast_node( ast_node_t* n, value_stack_t* stack );

//=============================================================================
// ビルトイン
typedef void (*command_delegate)( execute_environment_t* e, execute_status_t* s, int arg_num );

enum builtin_command_tag
{
	COMMAND_DEVTERM =0,// デバッグ用の隠し
	COMMAND_DIM,
	COMMAND_DDIM,
	COMMAND_SDIM,
	COMMAND_POKE,
	COMMAND_WPOKE,
	COMMAND_LPOKE,
	COMMAND_MES,
	COMMAND_INPUT,
	COMMAND_RANDOMIZE,
	COMMAND_BENCH,

	MAX_COMMAND,
};

int query_command( const char* s );
command_delegate get_command_delegate( builtin_command_tag command );

typedef void (*function_delegate)( execute_environment_t* e, execute_status_t* s, int arg_num );

enum builtin_function_tag
{
	FUNCTION_INT =0,
	FUNCTION_DOUBLE,
	FUNCTION_STR,
	FUNCTION_PEEK,
	FUNCTION_WPEEK,
	FUNCTION_LPEEK,
	FUNCTION_RND,
	FUNCTION_ABS,
	FUNCTION_ABSF,
	FUNCTION_DEG2RAD,
	FUNCTION_RAD2DEG,
	FUNCTION_SIN,
	FUNCTION_COS,
	FUNCTION_TAN,
	FUNCTION_ATAN,
	FUNCTION_EXPF,
	FUNCTION_LOGF,
	FUNCTION_POWF,
	FUNCTION_SQRT,
	FUNCTION_LIMIT,
	FUNCTION_LIMITF,
	FUNCTION_STRLEN,

	MAX_FUNCTION,
};

int query_function( const char* s );
function_delegate get_function_delegate( builtin_function_tag command );

//=============================================================================
// ユーティリティ
void dump_ast( list_t* ast, bool is_detail =false );
void dump_variable( list_t* var_table, const char* name, int idx );
void dump_stack( value_stack_t* stack );
void dump_code( const code_container_t* code );


}// namespace neteruhsp

