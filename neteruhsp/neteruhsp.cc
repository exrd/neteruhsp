
#include "neteruhsp.hh"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cmath>
#include <cstdint>

#include <cassert>
#include <cstdarg>

#if NHSP_CONFIG_PERFORMANCE_TIMER
#include <chrono>
#endif

namespace neteruhsp
{

#define NHSP_UNUA(v)
#define NHSP_UNUSE(v) ((void)v)

#define NHSP_MPI	(3.141592653589793238)

//=============================================================================
//=============================================================================
//=============================================================================
// 内部リンケージの人たち、ここから

namespace
{

#if NHSP_CONFIG_MEMLEAK_DETECTION
static list_t* s_memory_map_ = nullptr;
#endif

#if NHSP_CONFIG_VALUE_ALLOCATION_CACHE
// 一時的にスタックに溜まる分をキャッシュできれば十分なので、ある程度小さくてもよい
static const size_t MAX_VALUE_ALLOCATION_CACHE = 64;
static value_t* s_value_allocation_cache[ MAX_VALUE_ALLOCATION_CACHE ];
static int s_value_allocation_cache_idx = 0;
#endif

//=============================================================================
// メモリ
void* zmalloc( size_t size )
{
	return malloc( size );
}

void  zfree( void* ptr )
{
	free( ptr );
}

void* zrealloc( void* ptr, size_t size )
{
	return realloc( ptr, size );
}

void zregister_memory( void* ptr )
{
#if NHSP_CONFIG_MEMLEAK_DETECTION
	assert( s_memory_map_ != nullptr );
	const auto node = reinterpret_cast<list_node_t*>( zmalloc(sizeof(list_node_t)) );
	node->next_ = node->prev_ = nullptr;
	node->value_ = ptr;
	list_append( *s_memory_map_, node );
#else
	NHSP_UNUSE(ptr);
#endif
}

void zunregister_memory( void* ptr )
{
#if NHSP_CONFIG_MEMLEAK_DETECTION
	assert( s_memory_map_ != nullptr );
	const auto node = list_find( *s_memory_map_, ptr );
	assert( node != nullptr );
	list_erase( *s_memory_map_, node );
	zfree( node );
#else
	NHSP_UNUSE(ptr);
#endif
}

//=============================================================================
// エラー
void print_error( const char* message, ... )
{
	va_list args;
	va_start( args, message );
	vfprintf( stderr, message, args );
	va_end( args );
	fflush( stderr );
}

void raise_error( const char* message, ... )
{
	va_list args;
	va_start( args, message );
	vfprintf( stderr, message, args );
	va_end( args );
	fflush( stderr );
	exit( -1 );
}

//=============================================================================
// 文字列
char* create_string( size_t len )
{
	return reinterpret_cast<char*>( xmalloc( len +1 ) );
}

char* create_string( const char* s, size_t len )
{
	char* res = create_string( len );
	strncpy( res, s, len );
	res[len] = '\0';
	return res;
}

char* create_string( const char* s )
{
	return create_string( s, strlen( s ) );
}

void  destroy_string( char* s )
{
	xfree( s );
}

char* create_string_from( int v )
{
	const auto len = snprintf( nullptr, 0, "%d", v );
	auto res = create_string( len );
	sprintf( res, "%d", v );
	return res;
}

char* create_string_from( double v )
{
	const auto len = snprintf( nullptr, 0, "%lf", v );
	auto res = create_string( len );
	sprintf( res, "%lf", v );
	return res;
}

bool string_equal_igcase( const char* sl, const char* r, int len =-1 )
{
	const auto tol =[]( char c ) -> int
	{
		if ( c>='A' && c<='Z' )
		{ return c -'A' +'a'; }
		return c;
	};

	int iter =0;
	for( ; ; )
	{
		if ( sl[0]=='\0' || r[0]=='\0' )
		{
			if ( len < 0 )
			{
				if ( sl[0] != r[0] )
				{ return false; }
			}
			else
			{
				if ( sl[0]!='\0' || iter!=len )
				{ return false; }
			}
			break;
		}
		if ( tol( sl[0] ) != tol( r[0] ) )
		{ return false; }
		++sl, ++r; ++iter;
	}
	return true;
}

//=============================================================================
// 値
value_t* alloc_value()
{
#if NHSP_CONFIG_VALUE_ALLOCATION_CACHE
	if ( s_value_allocation_cache_idx < static_cast<int>(MAX_VALUE_ALLOCATION_CACHE) )
	{
		return s_value_allocation_cache[s_value_allocation_cache_idx++];
	}
#endif
	return reinterpret_cast<value_t*>( xmalloc( sizeof(value_t) ) );
}

void free_value( value_t* p )
{
#if NHSP_CONFIG_VALUE_ALLOCATION_CACHE
	if ( s_value_allocation_cache_idx > 0 )
	{
		s_value_allocation_cache[--s_value_allocation_cache_idx] = p;
		return;
	}
#endif
	xfree( p );
}

void clear_value( value_t* t )
{
	switch( t->type_ )
	{
		case VALUE_STRING:	destroy_string( t->svalue_ ); break;
		default: break;
	}
}

//=============================================================================
// コード生成
void code_checked_realloc( execute_environment_t* e, size_t size )
{
	const auto required = e->execute_code_->code_size_ +size;
	if ( e->execute_code_->code_buffer_size_ <= required )
	{
		static const size_t default_size = 256;
		if ( e->execute_code_->code_buffer_size_ < default_size )
		{
			e->execute_code_->code_buffer_size_ = default_size;
		}

		while( e->execute_code_->code_buffer_size_ <= required )
		{
			e->execute_code_->code_buffer_size_ *= 2;
		}

		const auto area_size = e->execute_code_->code_buffer_size_ *sizeof(code_t);
		e->execute_code_->code_ = reinterpret_cast<code_t*>( xrealloc( e->execute_code_->code_, area_size ) );
		if ( e->execute_code_->code_ == nullptr )
		{
			raise_error( "仮想マシン用のコードバッファが確保できません" );
		}
	}
}

template< typename T >
void code_write_block( execute_environment_t* e, const T block )
{
	const auto size = sizeof(block);
	const auto stride = size /sizeof(code_t) + (size %sizeof(code_t) == 0 ? 0 : 1);
	code_checked_realloc( e, stride );
	memcpy( e->execute_code_->code_ +e->execute_code_->code_size_, &block, size );
	e->execute_code_->code_size_ += stride;
}

void code_write( execute_environment_t* e, code_t code )
{
	code_checked_realloc( e, 1 );
	e->execute_code_->code_[e->execute_code_->code_size_++] = code;
}

void code_write( execute_environment_t* e, void* ptr )
{
	code_write_block( e, ptr );
}

template< typename T >
int code_get_block( T& block, const code_t* codes, int pc )
{
	const auto size = sizeof(block);
	const auto stride = size /sizeof(code_t) + (size %sizeof(code_t) == 0 ? 0 : 1);
	memcpy( &block, reinterpret_cast<const void*>(codes +pc), sizeof(block) );
	return stride;
}

//=============================================================================
// 実行環境ユーティリティ
label_node_t* search_label( execute_environment_t* e, const char* name )
{
	auto node = e->label_table_->head_;
	while( node != nullptr )
	{
		const auto label = reinterpret_cast<label_node_t*>( node->value_ );
		if ( string_equal_igcase( label->name_, name ) )
		{ return label; }

		node = node->next_;
	}
	return nullptr;
}

//=============================================================================
// コマンド実体
void command_devterm( execute_environment_t* NHSP_UNUA(e), execute_status_t* s, int arg_num )
{
	stack_pop( s->stack_, arg_num );
}

void command_dim( execute_environment_t* NHSP_UNUA(e), execute_status_t* s, int arg_num )
{
	if ( arg_num < 2 )
	{
		raise_error( "dim：引数がたりません" );
	}
	if ( arg_num > 2 )
	{
		raise_error( "dim：引数が多すぎます、配列変数は1次元までしかサポートしていません" );
	}

	const auto arg_start = -arg_num;
	const auto v = stack_peek( s->stack_, arg_start );
	if ( v->type_ != VALUE_VARIABLE )
	{
		raise_error( "dim：対象が変数ではありません" );
	}
	if ( v->index_ > 0 )
	{
		raise_error( "dim：対象の変数が配列として指定されています" );
	}

	const auto n = stack_peek( s->stack_, arg_start +1 );
	const auto num = value_calc_int( *n );
	if ( num <= 0 )
	{
		raise_error( "dim：0個以下の要素は確保できません" );
	}

	prepare_variable( v->variable_, VALUE_INT, 64, num );

	stack_pop( s->stack_, arg_num );
}

void command_ddim( execute_environment_t* NHSP_UNUA(e), execute_status_t* s, int arg_num )
{
	if ( arg_num < 2 )
	{
		raise_error( "ddim：引数がたりません" );
	}
	if ( arg_num > 2 )
	{
		raise_error( "ddim：引数が多すぎます、配列変数は1次元までしかサポートしていません" );
	}

	const auto arg_start = -arg_num;
	const auto v = stack_peek( s->stack_, arg_start );
	if ( v->type_ != VALUE_VARIABLE )
	{
		raise_error( "ddim：対象が変数ではありません" );
	}
	if ( v->index_ > 0 )
	{
		raise_error( "ddim：対象の変数が配列として指定されています" );
	}

	const auto n = stack_peek( s->stack_, arg_start +1 );
	const auto num = value_calc_int( *n );
	if ( num <= 0 )
	{
		raise_error( "ddim：0個以下の要素は確保できません" );
	}

	prepare_variable( v->variable_, VALUE_DOUBLE, 64, num );

	stack_pop( s->stack_, arg_num );
}

void command_sdim( execute_environment_t* NHSP_UNUA(e), execute_status_t* s, int arg_num )
{
	if ( arg_num < 2 )
	{
		raise_error( "sdim：引数がたりません" );
	}
	if ( arg_num > 3 )
	{
		raise_error( "sdim：引数が多すぎます、配列変数は1次元までしかサポートしていません" );
	}

	const auto arg_start = -arg_num;
	const auto v = stack_peek( s->stack_, arg_start );
	if ( v->type_ != VALUE_VARIABLE )
	{
		raise_error( "sdim：対象が変数ではありません" );
	}
	if ( v->index_ > 0 )
	{
		raise_error( "sdim：対象の変数が配列として指定されています" );
	}

	const auto g = stack_peek( s->stack_, arg_start +1 );
	const auto granule = value_calc_int( *g );

	const auto num = ( arg_num>2 ? value_calc_int( *stack_peek( s->stack_, arg_start +2 ) ) : 1 );
	if ( granule <= 0 )
	{
		raise_error( "sdim：0以下のサイズの文字列は作れません" );
	}
	if ( num <= 0 )
	{
		raise_error( "sdim：0個以下の要素は確保できません" );
	}

	prepare_variable( v->variable_, VALUE_STRING, granule, num );

	stack_pop( s->stack_, arg_num );
}

void command_poke( execute_environment_t* NHSP_UNUA(e), execute_status_t* s, int arg_num )
{
	if ( arg_num < 3 )
	{
		raise_error( "poke：引数がたりません" );
	}
	if ( arg_num > 3 )
	{
		raise_error( "poke：引数が多すぎます、配列変数は1次元までしかサポートしていません" );
	}

	const auto arg_start = -arg_num;
	const auto v = stack_peek( s->stack_, arg_start );
	if ( v->type_ != VALUE_VARIABLE )
	{
		raise_error( "poke：対象が変数ではありません" );
		return;
	}
	if ( v->index_ > 0 )
	{
		raise_error( "poke：対象の変数が配列として指定されています" );
		return;
	}

	auto* const var = v->variable_;

	const auto i = stack_peek( s->stack_, arg_start +1 );
	const auto byte_idx = value_calc_int( *i );

	const auto wp = stack_peek( s->stack_, arg_start +2 );
	const auto w = value_calc_int( *wp );

	if ( byte_idx<0 || var->data_size_<( byte_idx + 1 ) )
	{
		raise_error( "poke：対象の変数の範囲外を書き込もうとしています@@ %s(size=%d, idx=%d)", var->name_, var->data_size_, byte_idx );
		return;
	}

	auto* const dp = reinterpret_cast<char*>( var->data_ );
	dp[byte_idx] = static_cast<char>( w );

	stack_pop( s->stack_, arg_num );
}

void command_wpoke( execute_environment_t* NHSP_UNUA(e), execute_status_t* s, int arg_num )
{
	if ( arg_num < 3 )
	{
		raise_error( "wpoke：引数がたりません" );
	}
	if ( arg_num > 3 )
	{
		raise_error( "wpoke：引数が多すぎます、配列変数は1次元までしかサポートしていません" );
	}

	const auto arg_start = -arg_num;
	const auto v = stack_peek( s->stack_, arg_start );
	if ( v->type_ != VALUE_VARIABLE )
	{
		raise_error( "wpoke：対象が変数ではありません" );
		return;
	}
	if ( v->index_ > 0 )
	{
		raise_error( "wpoke：対象の変数が配列として指定されています" );
		return;
	}

	auto* const var = v->variable_;

	const auto i = stack_peek( s->stack_, arg_start +1 );
	const auto byte_idx = value_calc_int( *i );

	const auto wp = stack_peek( s->stack_, arg_start +2 );
	const std::int16_t w = static_cast<std::int16_t>( value_calc_int( *wp ) );

	if ( byte_idx<0 || var->data_size_<( byte_idx + 2 ) )
	{
		raise_error( "wpoke：対象の変数の範囲外を書き込もうとしています@@ %s(size=%d, idx=%d)", var->name_, var->data_size_, byte_idx );
		return;
	}

	auto* const dp = reinterpret_cast<char*>( var->data_ );
	memcpy( dp + byte_idx, &w, 2 );

	stack_pop( s->stack_, arg_num );
}

void command_lpoke( execute_environment_t* NHSP_UNUA(e), execute_status_t* s, int arg_num )
{
	if ( arg_num < 3 )
	{
		raise_error( "lpoke：引数がたりません" );
	}
	if ( arg_num > 3 )
	{
		raise_error( "lpoke：引数が多すぎます、配列変数は1次元までしかサポートしていません" );
	}

	const auto arg_start = -arg_num;
	const auto v = stack_peek( s->stack_, arg_start );
	if ( v->type_ != VALUE_VARIABLE )
	{
		raise_error( "lpoke：対象が変数ではありません" );
		return;
	}
	if ( v->index_ > 0 )
	{
		raise_error( "lpoke：対象の変数が配列として指定されています" );
		return;
	}

	auto* const var = v->variable_;

	const auto i = stack_peek( s->stack_, arg_start +1 );
	const auto byte_idx = value_calc_int( *i );

	const auto wp = stack_peek( s->stack_, arg_start +2 );
	const std::int32_t w = static_cast<std::int32_t>( value_calc_int( *wp ) );

	if ( byte_idx<0 || var->data_size_<( byte_idx + 4 ) )
	{
		raise_error( "lpoke：対象の変数の範囲外を書き込もうとしています@@ %s(size=%d, idx=%d)", var->name_, var->data_size_, byte_idx );
		return;
	}

	auto* const dp = reinterpret_cast<char*>( var->data_ );
	memcpy( dp + byte_idx, &w, 4 );

	stack_pop( s->stack_, arg_num );
}

void command_mes( execute_environment_t* NHSP_UNUA(e), execute_status_t* s, int arg_num )
{
	if ( arg_num <= 0 )
	{
		raise_error( "mes：引数がたりません" );
	}
	if ( arg_num > 1 )
	{
		raise_error( "mes：引数が多すぎます@@ %d個渡されました", arg_num );
	}

	const auto m = stack_peek( s->stack_ );
	value_isolate( *m );
	if ( m->type_ != VALUE_STRING )
	{
		raise_error( "mes：引数が文字列型ではありません" );
	}

	printf( "%s\n", m->svalue_ );

	stack_pop( s->stack_, arg_num );
}

void command_input( execute_environment_t* e, execute_status_t* s, int arg_num )
{
	if ( arg_num < 2 )
	{
		raise_error( "input：引数がたりません" );
	}
	if ( arg_num > 3 )
	{
		raise_error( "input：引数が多すぎます" );
	}

	const auto arg_start = -arg_num;
	const auto v = stack_peek( s->stack_, arg_start );
	if ( v->type_ != VALUE_VARIABLE )
	{
		raise_error( "input：対象が変数ではありません" );
	}
	if ( v->index_ > 0 )
	{
		raise_error( "input：対象の変数が配列として指定されています" );
	}

	const auto n = stack_peek( s->stack_, arg_start +1 );
	const auto len = value_calc_int( *n ) +1;

	const auto mode = ( arg_num>2 ? value_calc_int( *stack_peek( s->stack_, arg_start +2 ) ) : 0 );

	auto buf = create_string( len );
	int w =0;
	for( ; ; )
	{
		if ( w >= len )
		{ break; }

		const auto c = getchar();
		if ( c == EOF )
		{ break; }

		const auto ch = static_cast<char>( c );

		if ( mode==1 && ch=='\n' )
		{ break; }

		if ( mode==2 )
		{
			if ( ch == '\r' )
			{
				const auto nc =getchar();
				if ( static_cast<char>(nc) == '\n' )
				{ break; }
				ungetc( nc, stdin );
			}
			else if ( ch == '\n' )
			{ break; }
		}

		buf[w] = static_cast<char>( ch );
		++w;
	}
	buf[w] = '\0';

	auto t = create_value_move( buf );
	variable_set( e->variable_table_, *t, v->variable_->name_, 0 );
	destroy_value( t );

	s->strsize_ = w;

	stack_pop( s->stack_, arg_num );
}

void command_randomize( execute_environment_t* NHSP_UNUA(e), execute_status_t* s, int arg_num )
{
	if ( arg_num > 1 )
	{
		raise_error( "randomize：引数が多すぎます" );
	}

	unsigned int seed = 0;
	if ( arg_num == 0 )
	{
		seed = static_cast<unsigned int>(time(nullptr));
	}
	else
	{
		const auto m = stack_peek( s->stack_ );
		seed = static_cast<unsigned int>( value_calc_int( *m ) );
	}
	srand( seed );

	stack_pop( s->stack_, arg_num );
}

void command_bench( execute_environment_t* NHSP_UNUA(e), execute_status_t* s, int arg_num )
{
#if NHSP_CONFIG_PERFORMANCE_TIMER
	using clock = std::chrono::high_resolution_clock;
	static bool s_prev_time_point_valid = false;
	static std::chrono::time_point<clock> s_prev_time_point;

	const auto cur_time_point = clock::now();

	bool is_display = false;
	if ( arg_num > 0 )
	{
		const auto m = stack_peek( s->stack_ );
		is_display = value_calc_boolean( *m );
	}

	const auto diff = cur_time_point -s_prev_time_point;
	const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>( diff );
	if ( is_display && s_prev_time_point_valid )
	{
		printf( "bench[diff] %lld[us]\n", elapsed.count() );
	}

	stack_pop( s->stack_, arg_num );

	s->refdval_ = static_cast<double>( elapsed.count() );

	s_prev_time_point_valid =true;
	s_prev_time_point = cur_time_point;
#else
	assert( false );
	stack_pop( s->stack_, arg_num );
#endif
}

//=============================================================================
// 関数実体
void function_int( execute_environment_t* NHSP_UNUA(e), execute_status_t* s, int arg_num )
{
	if ( arg_num <= 0 )
	{
		raise_error( "int：引数がたりません" );
	}
	if ( arg_num > 1 )
	{
		raise_error( "int：引数が多すぎます@@ %d個渡されました", arg_num );
	}

	const auto m = stack_peek( s->stack_ );
	const auto r = value_calc_int( *m );

	stack_pop( s->stack_, arg_num );
	stack_push( s->stack_, create_value( r ) );
}

void function_double( execute_environment_t* NHSP_UNUA(e), execute_status_t* s, int arg_num )
{
	if ( arg_num <= 0 )
	{
		raise_error( "double：引数がたりません" );
	}
	if ( arg_num > 1 )
	{
		raise_error( "double：引数が多すぎます@@ %d個渡されました", arg_num );
	}

	const auto m = stack_peek( s->stack_ );
	const auto r = value_calc_double( *m );

	stack_pop( s->stack_, arg_num );
	stack_push( s->stack_, create_value( r ) );
}

void function_str( execute_environment_t* NHSP_UNUA(e), execute_status_t* s, int arg_num )
{
	if ( arg_num <= 0 )
	{
		raise_error( "str：引数がたりません" );
	}
	if ( arg_num > 1 )
	{
		raise_error( "str：引数が多すぎます@@ %d個渡されました", arg_num );
	}

	const auto m = stack_peek( s->stack_ );
	const auto r = value_calc_string( *m );

	stack_pop( s->stack_, arg_num );
	stack_push( s->stack_, create_value_move( r ) );
}

void function_peek( execute_environment_t* NHSP_UNUA(e), execute_status_t* s, int arg_num )
{
	if ( arg_num <= 1 )
	{
		raise_error( "peek：引数がたりません" );
	}
	if ( arg_num > 2 )
	{
		raise_error( "peek：引数が多すぎます@@ %d個渡されました", arg_num );
	}

	const auto arg_start = -arg_num;
	const auto v = stack_peek( s->stack_, arg_start );
	if ( v->type_ != VALUE_VARIABLE )
	{
		raise_error( "peek：対象が変数ではありません" );
	}
	if ( v->index_ > 0 )
	{
		raise_error( "peek：対象の変数が配列として指定されています" );
		return;
	}

	const auto* const var = v->variable_;

	const auto n = stack_peek( s->stack_, arg_start +1 );
	const auto byte_idx = value_calc_int( *n );

	if ( byte_idx<0 || var->data_size_<( byte_idx + 1 ) )
	{
		raise_error( "peek：対象の変数の範囲外を読もうとしています@@ %s(size=%d, idx=%d)", var->name_, var->data_size_, byte_idx );
		return;
	}

	const auto* const dp = reinterpret_cast<const char*>( var->data_ );
	const int res = dp[byte_idx];

	stack_pop( s->stack_, arg_num );
	stack_push( s->stack_, create_value( res ) );
}

void function_wpeek( execute_environment_t* NHSP_UNUA(e), execute_status_t* s, int arg_num )
{
	if ( arg_num <= 1 )
	{
		raise_error( "wpeek：引数がたりません" );
	}
	if ( arg_num > 2 )
	{
		raise_error( "wpeek：引数が多すぎます@@ %d個渡されました", arg_num );
	}

	const auto arg_start = -arg_num;
	const auto v = stack_peek( s->stack_, arg_start );
	if ( v->type_ != VALUE_VARIABLE )
	{
		raise_error( "wpeek：対象が変数ではありません" );
	}
	if ( v->index_ > 0 )
	{
		raise_error( "wpeek：対象の変数が配列として指定されています" );
		return;
	}

	const auto* const var = v->variable_;

	const auto n = stack_peek( s->stack_, arg_start +1 );
	const auto byte_idx = value_calc_int( *n );

	if ( byte_idx<0 || var->data_size_<( byte_idx + 2 ) )
	{
		raise_error( "wpeek：対象の変数の範囲外を読もうとしています@@ %s(size=%d, idx=%d)", var->name_, var->data_size_, byte_idx );
		return;
	}

	const auto* const dp = reinterpret_cast<const char*>( var->data_ );
	std::int16_t res =0;
	memcpy(&res, dp + byte_idx, 2);

	stack_pop( s->stack_, arg_num );
	stack_push( s->stack_, create_value( static_cast<int>( res ) ) );
}

void function_lpeek( execute_environment_t* NHSP_UNUA(e), execute_status_t* s, int arg_num )
{
	if ( arg_num <= 1 )
	{
		raise_error( "lpeek：引数がたりません" );
	}
	if ( arg_num > 2 )
	{
		raise_error( "lpeek：引数が多すぎます@@ %d個渡されました", arg_num );
	}

	const auto arg_start = -arg_num;
	const auto v = stack_peek( s->stack_, arg_start );
	if ( v->type_ != VALUE_VARIABLE )
	{
		raise_error( "lpeek：対象が変数ではありません" );
	}
	if ( v->index_ > 0 )
	{
		raise_error( "lpeek：対象の変数が配列として指定されています" );
		return;
	}

	const auto* const var = v->variable_;

	const auto n = stack_peek( s->stack_, arg_start +1 );
	const auto byte_idx = value_calc_int( *n );

	if ( byte_idx<0 || var->data_size_<( byte_idx + 4 ) )
	{
		raise_error( "lpeek：対象の変数の範囲外を読もうとしています@@ %s(size=%d, idx=%d)", var->name_, var->data_size_, byte_idx );
		return;
	}

	const auto* const dp = reinterpret_cast<const char*>( var->data_ );
	std::int32_t res =0;
	memcpy(&res, dp + byte_idx, 4);

	stack_pop( s->stack_, arg_num );
	stack_push( s->stack_, create_value( static_cast<int>( res ) ) );
}

void function_rnd( execute_environment_t* NHSP_UNUA(e), execute_status_t* s, int arg_num )
{
	if ( arg_num <= 0 )
	{
		raise_error( "rnd：引数がたりません" );
	}
	if ( arg_num > 1 )
	{
		raise_error( "rnd：引数が多すぎます@@ %d個渡されました", arg_num );
	}

	const auto m = stack_peek( s->stack_ );
	const auto r = value_calc_int( *m );
	if ( r < 1 )
	{
		raise_error( "rnd：引数は1以上である必要があります@@ %d", r );
	}

	stack_pop( s->stack_, arg_num );
	const auto res = rand() %(r);
	stack_push( s->stack_, create_value( res ) );
}

void function_abs( execute_environment_t* NHSP_UNUA(e), execute_status_t* s, int arg_num )
{
	if ( arg_num <= 0 )
	{
		raise_error( "abs：引数がたりません" );
	}
	if ( arg_num > 1 )
	{
		raise_error( "abs：引数が多すぎます@@ %d個渡されました", arg_num );
	}

	const auto m = stack_peek( s->stack_ );
	const auto r = value_calc_int( *m );

	stack_pop( s->stack_, arg_num );
	const auto res = ( r<0 ? -r : r );
	stack_push( s->stack_, create_value( res ) );
}

void function_absf( execute_environment_t* NHSP_UNUA(e), execute_status_t* s, int arg_num )
{
	if ( arg_num <= 0 )
	{
		raise_error( "absf：引数がたりません" );
	}
	if ( arg_num > 1 )
	{
		raise_error( "absf：引数が多すぎます@@ %d個渡されました", arg_num );
	}

	const auto m = stack_peek( s->stack_ );
	const auto r = value_calc_double( *m );

	stack_pop( s->stack_, arg_num );
	const auto res = ( r<0.0 ? -r : r );
	stack_push( s->stack_, create_value( res ) );
}

void function_deg2rad( execute_environment_t* NHSP_UNUA(e), execute_status_t* s, int arg_num )
{
	if ( arg_num <= 0 )
	{
		raise_error( "deg2rad：引数がたりません" );
	}
	if ( arg_num > 1 )
	{
		raise_error( "deg2rad：引数が多すぎます@@ %d個渡されました", arg_num );
	}

	const auto m = stack_peek( s->stack_ );
	const auto r = value_calc_double( *m );

	stack_pop( s->stack_, arg_num );
	const auto res = ( r * NHSP_MPI / 180.0 );
	stack_push( s->stack_, create_value( res ) );
}

void function_rad2deg( execute_environment_t* NHSP_UNUA(e), execute_status_t* s, int arg_num )
{
	if ( arg_num <= 0 )
	{
		raise_error( "rad2deg：引数がたりません" );
	}
	if ( arg_num > 1 )
	{
		raise_error( "rad2deg：引数が多すぎます@@ %d個渡されました", arg_num );
	}

	const auto m = stack_peek( s->stack_ );
	const auto r = value_calc_double( *m );

	stack_pop( s->stack_, arg_num );
	const auto res = ( r * 180.0 / NHSP_MPI );
	stack_push( s->stack_, create_value( res ) );
}

void function_sin( execute_environment_t* NHSP_UNUA(e), execute_status_t* s, int arg_num )
{
	if ( arg_num <= 0 )
	{
		raise_error( "sin：引数がたりません" );
	}
	if ( arg_num > 1 )
	{
		raise_error( "sin：引数が多すぎます@@ %d個渡されました", arg_num );
	}

	const auto m = stack_peek( s->stack_ );
	const auto r = value_calc_double( *m );

	stack_pop( s->stack_, arg_num );
	const auto res = std::sin( r );
	stack_push( s->stack_, create_value( res ) );
}

void function_cos( execute_environment_t* NHSP_UNUA(e), execute_status_t* s, int arg_num )
{
	if ( arg_num <= 0 )
	{
		raise_error( "cos：引数がたりません" );
		return;
	}
	if ( arg_num > 1 )
	{
		raise_error( "cos：引数が多すぎます@@ %d個渡されました", arg_num );
		return;
	}

	const auto m = stack_peek( s->stack_ );
	const auto r = value_calc_double( *m );

	stack_pop( s->stack_, arg_num );
	const auto res = std::cos( r );
	stack_push( s->stack_, create_value( res ) );
}

void function_tan( execute_environment_t* NHSP_UNUA(e), execute_status_t* s, int arg_num )
{
	if ( arg_num <= 0 )
	{
		raise_error( "tan：引数がたりません" );
	}
	if ( arg_num > 1 )
	{
		raise_error( "tan：引数が多すぎます@@ %d個渡されました", arg_num );
	}

	const auto m = stack_peek( s->stack_ );
	const auto r = value_calc_double( *m );

	stack_pop( s->stack_, arg_num );
	const auto res = std::tan( r );
	stack_push( s->stack_, create_value( res ) );
}

void function_atan( execute_environment_t* NHSP_UNUA(e), execute_status_t* s, int arg_num )
{
	if ( arg_num < 2 )
	{
		raise_error( "atan：引数がたりません" );
	}
	if ( arg_num > 2 )
	{
		raise_error( "atan：引数が多すぎます@@ %d個渡されました", arg_num );
	}

	const auto arg_start = -arg_num;
	const auto vy = stack_peek( s->stack_, arg_start + 0 );
	const auto vx = stack_peek( s->stack_, arg_start + 1 );
	const auto y = value_calc_double( *vy );
	const auto x = value_calc_double( *vx );

	stack_pop( s->stack_, arg_num );
	const auto res = std::atan2( y, x );
	stack_push( s->stack_, create_value( res ) );
}

void function_expf( execute_environment_t* NHSP_UNUA(e), execute_status_t* s, int arg_num )
{
	if ( arg_num <= 0 )
	{
		raise_error( "expf：引数がたりません" );
	}
	if ( arg_num > 1 )
	{
		raise_error( "expf：引数が多すぎます@@ %d個渡されました", arg_num );
	}

	const auto m = stack_peek( s->stack_ );
	const auto r = value_calc_double( *m );

	stack_pop( s->stack_, arg_num );
	const auto res = std::exp( r );
	stack_push( s->stack_, create_value( res ) );
}

void function_logf( execute_environment_t* NHSP_UNUA(e), execute_status_t* s, int arg_num )
{
	if ( arg_num <= 0 )
	{
		raise_error( "logf：引数がたりません" );
	}
	if ( arg_num > 1 )
	{
		raise_error( "logf：引数が多すぎます@@ %d個渡されました", arg_num );
	}

	const auto m = stack_peek( s->stack_ );
	const auto r = value_calc_double( *m );

	stack_pop( s->stack_, arg_num );
	const auto res = std::log( r );
	stack_push( s->stack_, create_value( res ) );
}

void function_powf( execute_environment_t* NHSP_UNUA(e), execute_status_t* s, int arg_num )
{
	if ( arg_num <= 0 )
	{
		raise_error( "powf：引数がたりません" );
	}
	if ( arg_num > 2 )
	{
		raise_error( "powf：引数が多すぎます@@ %d個渡されました", arg_num );
	}

	const auto arg_start = -arg_num;
	const auto vx = stack_peek( s->stack_, arg_start + 0 );
	const auto vy = stack_peek( s->stack_, arg_start + 1 );
	const auto x = value_calc_double( *vx );
	const auto y = value_calc_double( *vy );

	stack_pop( s->stack_, arg_num );
	const auto res = std::pow( x, y );
	stack_push( s->stack_, create_value( res ) );
}

void function_sqrt( execute_environment_t* NHSP_UNUA(e), execute_status_t* s, int arg_num )
{
	if ( arg_num <= 0 )
	{
		raise_error( "sqrt：引数がたりません" );
	}
	if ( arg_num > 1 )
	{
		raise_error( "sqrt：引数が多すぎます@@ %d個渡されました", arg_num );
	}

	const auto m = stack_peek( s->stack_ );
	const auto r = value_calc_double( *m );

	stack_pop( s->stack_, arg_num );
	const auto res = std::sqrt( r );
	stack_push( s->stack_, create_value( res ) );
}

void function_limit( execute_environment_t* NHSP_UNUA(e), execute_status_t* s, int arg_num )
{
	if ( arg_num < 3 )
	{
		raise_error( "limit：引数がたりません" );
	}
	if ( arg_num > 3 )
	{
		raise_error( "limit：引数が多すぎます@@ %d個渡されました", arg_num );
	}

	const auto arg_start = -arg_num;
	const auto vmi = stack_peek( s->stack_, arg_start + 0 );
	const auto vm = stack_peek( s->stack_, arg_start + 1 );
	const auto vma = stack_peek( s->stack_, arg_start + 2 );
	const auto mi = value_calc_int( *vmi );
	const auto v = value_calc_int( *vm );
	const auto ma = value_calc_int( *vma );

	stack_pop( s->stack_, arg_num );
	auto res = v;
	if ( res < mi )
	{ res = mi; }
	if ( res > ma )
	{ res = ma; }
	stack_push( s->stack_, create_value( res ) );
}

void function_limitf( execute_environment_t* NHSP_UNUA(e), execute_status_t* s, int arg_num )
{
	if ( arg_num < 3 )
	{
		raise_error( "limitf：引数がたりません" );
	}
	if ( arg_num > 3 )
	{
		raise_error( "limitf：引数が多すぎます@@ %d個渡されました", arg_num );
	}

	const auto arg_start = -arg_num;
	const auto vmi = stack_peek( s->stack_, arg_start + 0 );
	const auto vm = stack_peek( s->stack_, arg_start + 1 );
	const auto vma = stack_peek( s->stack_, arg_start + 2 );
	const auto mi = value_calc_double( *vmi );
	const auto v = value_calc_double( *vm );
	const auto ma = value_calc_double( *vma );

	stack_pop( s->stack_, arg_num );
	auto res = v;
	if ( res < mi )
	{ res = mi; }
	if ( res > ma )
	{ res = ma; }
	stack_push( s->stack_, create_value( res ) );
}

void function_strlen( execute_environment_t* NHSP_UNUA(e), execute_status_t* s, int arg_num )
{
	if ( arg_num <= 0 )
	{
		raise_error( "strlen：引数がたりません" );
	}
	if ( arg_num > 1 )
	{
		raise_error( "strlen：引数が多すぎます" );
	}

	const auto m = stack_peek( s->stack_ );
	if ( value_get_primitive_tag( *m ) != VALUE_STRING )
	{
		raise_error( "strlen：引数が文字列型ではありません" );
	}
	const auto* const str = value_get_string( *m );
	assert( str != nullptr );

	stack_pop( s->stack_, arg_num );
	const auto res = static_cast<int>( strlen( str ) );
	stack_push( s->stack_, create_value( res ) );
}

}// namespace

//=============================================================================
//=============================================================================
//=============================================================================
// 外部リンケージを持つ人たち、ここから

//=============================================================================
// 全体
static bool s_is_system_initialized =false;

void initialize_system()
{
	assert( !s_is_system_initialized );
	s_is_system_initialized = true;

	// UTF8 ワークアラウンド
	//setvbuf( stdout, nullptr, _IOFBF, 1024 );
	setvbuf( stderr, nullptr, _IOFBF, 1024 );

#if NHSP_CONFIG_MEMLEAK_DETECTION
	if ( s_memory_map_ == nullptr )
	{
		s_memory_map_ = reinterpret_cast<list_t*>( zmalloc( sizeof(list_t) ) );
		s_memory_map_->head_ = s_memory_map_->tail_ = nullptr;
	}
#endif
#if NHSP_CONFIG_VALUE_ALLOCATION_CACHE
	for( size_t i=0; i<MAX_VALUE_ALLOCATION_CACHE; ++i )
	{
		s_value_allocation_cache[i] = reinterpret_cast<value_t*>( xmalloc( sizeof(value_t) ) );
	}
	s_value_allocation_cache_idx = 0;
#endif
}

void uninitialize_system()
{
	assert( s_is_system_initialized );
	s_is_system_initialized = false;

#if NHSP_CONFIG_VALUE_ALLOCATION_CACHE
	assert( s_value_allocation_cache_idx == 0 );
	for( size_t i=0; i<MAX_VALUE_ALLOCATION_CACHE; ++i )
	{
		if ( s_value_allocation_cache[i] != nullptr )
		{
			xfree( s_value_allocation_cache[i] );
			s_value_allocation_cache[i] = nullptr;
		}
	}
	s_value_allocation_cache_idx = 0;
#endif
#if NHSP_CONFIG_MEMLEAK_DETECTION
	if ( s_memory_map_ != nullptr )
	{
		printf( "====leaked memories\n" );
		auto node = s_memory_map_->head_;
		while( node != nullptr )
		{
			printf( "[%p]\n", node->value_ );
			node = node->next_;
		}
		printf( "----" );
	}
#endif
}

//=============================================================================
// メモリ

void* xmalloc( size_t size )
{
	const auto res = zmalloc( size );
#if NHSP_CONFIG_MEMLEAK_DETECTION
	{
		//printf( "xmalloc[%p]\n", res );
		if ( res != nullptr )
		{ zregister_memory( res ); }
	}
#endif
	return res;
}

void  xfree( void* ptr )
{
#if NHSP_CONFIG_MEMLEAK_DETECTION
	{
		//printf( "xfree[%p]\n", ptr );
		zunregister_memory( ptr );
	}
#endif
	zfree( ptr );
}

void* xrealloc( void* ptr, size_t size )
{
	const auto res = zrealloc( ptr, size );
#if NHSP_CONFIG_MEMLEAK_DETECTION
	{
		//printf( "xrealloc[%p->%p]\n", ptr, res );
		if ( res != ptr )
		{
			if ( ptr != nullptr )
			{ zunregister_memory( ptr ); }
			if ( res != nullptr )
			{ zregister_memory( res ); }
		}
	}
#endif
	return res;
}

//=============================================================================
// 文字列バッファ
string_buffer_t* create_string_buffer( size_t initial_len, int expand_step )
{
	assert( initial_len > 0 );
	auto* const res = reinterpret_cast<string_buffer_t*>( xmalloc( sizeof( string_buffer_t ) ) );
	initialize_string_buffer( res, initial_len, expand_step );
	return res;
}

void destroy_string_buffer( string_buffer_t* sb )
{
	uninitialize_string_buffer( sb );
	xfree( sb );
}

void initialize_string_buffer( string_buffer_t* sb, size_t initial_len, int expand_step )
{
	sb->buffer_ = create_string( initial_len );
	sb->buffer_size_ = static_cast<int>( initial_len );
	sb->cursor_ = 0;
	sb->expand_step_ = expand_step;
	sb->buffer_[0] = '\0';
}

void uninitialize_string_buffer( string_buffer_t* sb )
{
	assert( sb != nullptr );

	if ( sb->buffer_ != nullptr )
	{
		xfree( sb->buffer_ );
		sb->buffer_ = nullptr;
	}
	sb->buffer_size_ = 0;
	sb->cursor_ = 0;
	sb->expand_step_ = 0;
}

void string_buffer_append( string_buffer_t* sb, const char* s, int len )
{
	if ( len < 0 )
	{
		len = static_cast<int>( strlen( s ) );
	}

	if ( len == 0 )
	{
		return;
	}

	const auto left = sb->buffer_size_ - sb->cursor_;
	const auto required = len + 4;
	if ( left < required )
	{
		const int nexpand = ( required - left );
		const int expand_step = ( sb->expand_step_ <= 0 ? sb->buffer_size_/* x2 */ : sb->expand_step_ );
		const int expand = ( expand_step < nexpand ? nexpand : expand_step );
		const size_t nsize = sb->buffer_size_ + expand;
		sb->buffer_ = reinterpret_cast<char*>( xrealloc( sb->buffer_, static_cast<size_t>( nsize ) ) );
		sb->buffer_size_ = static_cast<int>( nsize );
	}

	memcpy( sb->buffer_ + sb->cursor_, s, len );
	sb->cursor_ += len;
	sb->buffer_[sb->cursor_] = '\0';
}

//=============================================================================
// リスト
list_node_t* create_list_node()
{
	list_node_t* res = reinterpret_cast<list_node_t*>( xmalloc( sizeof( list_node_t ) ) );
	res->prev_ = res->next_ = nullptr;
	res->value_ = nullptr;
	return res;
}

void destroy_list_node( list_node_t *node )
{
	unlink_list_node( node );
	xfree( node );
}

void link_next( list_node_t* node, list_node_t* list )
{
	assert( node->prev_==nullptr && node->next_==nullptr );
	node->prev_ = list;
	node->next_ = list->next_;
	list->next_ = node;
}

void link_prev( list_node_t* node, list_node_t* list )
{
	assert( node->prev_==nullptr && node->next_==nullptr );
	node->prev_ = list->prev_;
	node->next_ = list;
	list->prev_ = node;
}

bool unlink_list_node( list_node_t* node )
{
	if ( node->prev_==nullptr && node->next_==nullptr )
	{ 
		return false;
	}
	if ( node->prev_ != nullptr )
	{
		node->prev_->next_ = node->next_;
	}
	if ( node->next_ != nullptr )
	{
		node->next_->prev_ = node->prev_;
	}
	node->prev_ = node->next_ = nullptr;
	return true;
}

list_t* create_list()
{
	auto res = reinterpret_cast<list_t*>( xmalloc( sizeof(list_t) ) );
	res->head_ = res->tail_ = nullptr;
	return res;
}

void destroy_list( list_t* list )
{
	xfree( list );
}

void list_prepend( list_t& list, list_node_t* node )
{
	if ( list.head_ == nullptr )
	{
		assert( list.tail_==nullptr );
		list.head_ = list.tail_ = node;
		return;
	}
	link_prev( node, list.head_ );
	list.head_ = node;
}

void list_append( list_t& list, list_node_t* node )
{
	if ( list.tail_ == nullptr )
	{
		assert( list.head_==nullptr );
		list.head_ = list.tail_ = node;
		return;
	}
	link_next( node, list.tail_ );
	list.tail_ = node;
}

void list_erase( list_t& list, list_node_t* node )
{
	if ( list.head_ == node )
	{
		list.head_ = node->next_;
	}
	if ( list.tail_ == node )
	{
		list.tail_ = node->prev_;
	}
	unlink_list_node( node );
}

list_node_t* list_find( list_t& list, void* value )
{
	auto* node = list.head_;
	while( node != nullptr )
	{
		if ( node->value_ == value )
		{
			return node;
		}
		node = node->next_;
	}
	return nullptr;
}

void list_free_all( list_t& list )
{
	auto* node = list.head_;
	while( node != nullptr )
	{
		auto next = node->next_;
		list_erase( list, node );
		xfree( node );
		node = next;
	}
}

//=============================================================================
// キーワード
int query_keyword( const char* s )
{
	static const struct
	{
		int				tag_;
		const char*		word_;
	} table[] =
	{
		{ KEYWORD_GLOBAL,	"global", },
		{ KEYWORD_CTYPE,	"ctype", },
		{ KEYWORD_END,		"end", },
		{ KEYWORD_RETURN,	"return", },
		{ KEYWORD_GOTO,		"goto", },
		{ KEYWORD_GOSUB,	"gosub", },
		{ KEYWORD_REPEAT,	"repeat", },
		{ KEYWORD_LOOP,		"loop", },
		{ KEYWORD_CONTINUE,	"continue", },
		{ KEYWORD_BREAK,	"break", },
		{ KEYWORD_IF,		"if", },
		{ KEYWORD_ELSE,		"else", },
		{ KEYWORD_UNDEF,	nullptr },
	};

	// 全探索
	for( int i=0; table[i].tag_!=-1; ++i )
	{
		if ( string_equal_igcase( s, table[i].word_ ) )
		{
			return table[i].tag_;
		}
	}
	return KEYWORD_UNDEF;
}

//=============================================================================
// トークナイザ
int query_token_shadow( const char* ident, size_t len )
{
	static const struct
	{
		int				type_;
		const char*		word_;
	}
	shadows[] =
	{
		{ TOKEN_OP_NEQ,		"not" },
		{ TOKEN_OP_BAND,	"and" },
		{ TOKEN_OP_BOR,		"or" },
		{ TOKEN_OP_BXOR,	"xor" },
		{ TOKEN_UNKNOWN,	nullptr },
	};

	for( int i=0; shadows[i].type_!=-1; ++i )
	{
		if ( string_equal_igcase( shadows[i].word_, ident, static_cast<int>( len ) ) )
		{
			return shadows[i].type_;
		}
	}
	return TOKEN_UNKNOWN;
}

void initialize_tokenize_context( tokenize_context_t* c, const char* script )
{
	c->script_ = script;
	c->cursor_ = 0;
	c->line_ = 0;
	c->line_head_ = script;
}

void uninitialize_tokenize_context( tokenize_context_t* c )
{
	c->script_ = nullptr;
	c->cursor_ = 0;
	c->line_ = 0;
	c->line_head_ = nullptr;
}

token_t* get_token( tokenize_context_t& c )
{
	token_t* res = reinterpret_cast<token_t*>( xmalloc( sizeof( token_t ) ) );
	res->tag_ = TOKEN_UNKNOWN;
	res->content_ = nullptr;
	res->cursor_begin_ = c.cursor_;
	res->cursor_end_ = c.cursor_;
	res->left_space_ =false;
	res->right_space_ =false;

	const auto is_space = []( char c ) { return ( c==' ' || c=='\t' ); };
	const auto is_number = []( char c ) { return ( c>='0' && c<='9' ); };
	const auto is_alpha = []( char c ) { return ( ( c>='a' && c<='z' ) || ( c>='A' && c<='Z' ) ); };
	const auto is_rest_ident = [&]( char c ) { return ( is_number(c) || is_alpha(c) || c=='_' ); };

	const auto pp = c.script_ +c.cursor_;
	const char* p = c.script_ +c.cursor_;

restart:
	const auto prev_p = p;
	const auto prev_cursor = static_cast<int>( p - c.script_ );
	res->appear_line_ = c.line_;
	switch( p[0] )
	{
			// EOF
		case '\0':	res->tag_ = TOKEN_EOF; break;

			// 行終わり
		case '\r':
		case '\f':
			++p;
			goto restart;

		case '\n':
			// この位置はマーキング
			++p;
			++c.line_;
			c.line_head_ = p;
			res->tag_ = TOKEN_EOL;
			break;

			// ステートメント終わり
		case ':':
			++p;
			res->tag_ = TOKEN_EOS;
			break;

			// プリプロセッサで使う
		case '%':	++p; res->tag_ = TOKEN_PP_ARG_INDICATOR; break;

			// 微妙な文字
		case '{':	++p; res->tag_ = TOKEN_LBRACE; break;
		case '}':	++p; res->tag_ = TOKEN_RBRACE; break;
		case '(':	++p; res->tag_ = TOKEN_LPARENTHESIS; break;
		case ')':	++p; res->tag_ = TOKEN_RPARENTHESIS; break;
		case ',':	++p; res->tag_ = TOKEN_COMMA; break;

			// 演算子
		case '|':	++p; if ( p[0] == '=' ) { ++p; res->tag_ = TOKEN_BOR_ASSIGN; } else { res->tag_ = TOKEN_OP_BOR; } break;
		case '&':	++p; if ( p[0] == '=' ) { ++p; res->tag_ = TOKEN_BAND_ASSIGN; } else { res->tag_ = TOKEN_OP_BAND; } break;
		case '^':	++p; if ( p[0] == '=' ) { ++p; res->tag_ = TOKEN_BXOR_ASSIGN; } else { res->tag_ = TOKEN_OP_BXOR; } break;
		case '!':	++p; if ( p[0]=='=' ) { ++p; } res->tag_ = TOKEN_OP_NEQ; break;
		case '>':
			++p;
			if ( p[0] == '=' )
			{ ++p; res->tag_ = TOKEN_OP_GTOE; }
			else
			{ res->tag_ = TOKEN_OP_GT; }
			break;
		case '<':
			++p;
			if ( p[0] == '=' )
			{ ++p; res->tag_ = TOKEN_OP_LTOE; }
			else
			{ res->tag_ = TOKEN_OP_LT; }
			break;
		case '+':	++p; if ( p[0] == '=' ) { ++p; res->tag_ = TOKEN_ADD_ASSIGN; } else { res->tag_ = TOKEN_OP_ADD; } break;
		case '-':	++p; if ( p[0] == '=' ) { ++p; res->tag_ = TOKEN_SUB_ASSIGN; } else { res->tag_ = TOKEN_OP_SUB; } break;
		case '*':	++p; if ( p[0] == '=' ) { ++p; res->tag_ = TOKEN_MUL_ASSIGN; } else { res->tag_ = TOKEN_OP_MUL; } break;
		case '/':
			++p;
			if ( p[0] == '/' )// 一行コメント
			{
				++p;
				while( p[0]!='\n' && p[0]!='\0' )
				{ ++p; }
				goto restart;
			}
			if ( p[0] == '*' )// 複数行コメント
			{
				++p;
				for( ; ; )
				{
					if ( p[0] == '\0' )
					{
						raise_error( "複数行コメントの読み取り中にEOFが検出されました@@ %d行目", c.line_ );
						break;
					}
					if ( p[0] == '\n' )
					{
						++p;
						++c.line_;
						c.line_head_ = p;
						continue;
					}
					if ( p[0]=='*' && p[1]=='/' )
					{
						p += 2;
						break;
					}
					++p;
				}
				goto restart;
			}
			if ( p[0] == '=' )
			{
				++p;
				res->tag_ = TOKEN_DIV_ASSIGN;
			}
			else
			{ 
				res->tag_ = TOKEN_OP_DIV;
			}
			break;
		case '\\':	++p; if ( p[0] == '=' ) { ++p; res->tag_ = TOKEN_MOD_ASSIGN; } else { res->tag_ = TOKEN_OP_MOD; } break;

			// 代入
		case '=':
			++p;
			if ( p[0] == '=' )
			{
				++p;
				res->tag_ = TOKEN_OP_EQ;
			}
			else
			{
				res->tag_ = TOKEN_ASSIGN;
			}
			break;

			// 文字列
		case '\"':
		{
			++p;
			const auto s = p;
			while( p[0]!='\"' )
			{
				if ( p[0] == '\0' )
				{
					raise_error( "文字列の読み取り中にEOFが検出されました@@ %d行目", c.line_ );
				}
				if ( p[0]=='\\' && p[1]=='\"' )
				{
					++p;
				}
				++p;
			}
			const auto e = p;
			res->content_ = create_token_string( s, e -s );
			res->tag_ = TOKEN_STRING;
			++p;
			break;
		}

			// コメント
		case ';':
			++p;
			while( p[0]!='\n' && p[0]!='\0' )
			{
				++p;
			}
			goto restart;

		default:
			if ( is_space( p[0] ) )
			{
				// スペース
				++p;
				res->left_space_ =true;
				while( is_space(p[0]) )
				{
					++p;
				}
				goto restart;
			}
			else if ( is_number( p[0] ) )
			{
				// 数値
				if ( p[0] == '0' )
				{
					++p;
				}
				else
				{
					while( is_number(p[0]) )
					{
						++p;
					}
				}
				if ( p[0] == '.' )
				{
					++p;
					while( is_number(p[0]) )
					{
						++p;
					}
					res->tag_ = TOKEN_REAL;
				}
				else
				{
					res->tag_ = TOKEN_INTEGER;
				}
			}
			else if ( is_alpha( p[0] ) )
			{
				// 何らかの識別子
				++p;
				while( is_rest_ident( p[0] ) )
				{
					++p;
				}
				res->tag_ = TOKEN_IDENTIFIER;
				const auto shadow =query_token_shadow( c.script_ +prev_cursor, p -prev_p );
				if ( shadow != -1 )
				{
					res->tag_ = static_cast<token_tag>( shadow );
				}
			}
			else
			{
				// もう読めない
				raise_error( "読み取れない文字[%c]@@ %d行目", p[0], c.line_ );
			}
			break;
	}

	if ( is_space( p[0] ) )
	{
		res->right_space_ = true;
	}

	c.cursor_ += static_cast<int>( p - pp );
	res->cursor_begin_ = prev_cursor;
	res->cursor_end_ = c.cursor_;

	// contentが埋まってないなら埋める
	if ( res->content_ == nullptr )
	{
		const auto len = res->cursor_end_ -res->cursor_begin_;
		res->content_ = create_string( len );
		memcpy( res->content_, c.script_ +res->cursor_begin_, len );
		res->content_[len] = '\0';
	}

	return res;
}

void destroy_token( token_t* t )
{
	if ( t->content_ != nullptr )
	{
		destroy_string( t->content_ );
		t->content_ = nullptr;
	}
	xfree( t );
}

char* create_token_string( const char* str, size_t len )
{
	char* res = create_string( len );

	size_t w =0;
	for( size_t i=0; i<len; ++i, ++w )
	{
		if ( str[i]=='\\' && (i+1)<len )
		{
			switch( str[i+1] )
			{
				case 't':	res[w] = '\t'; break;
				case 'n':	res[w] = '\n'; break;
				case '\"':	res[w] = '\"'; break;
				default:
					raise_error( "読み取れないエスケープシーケンス@@ %c%c", str[i], str[i+1] );
					break;
			}
			++i;
		}
		else
		{
			res[w] = str[i];
		}
	}
	res[w] = '\0';
	return res;
}

//=============================================================================
// パーサ
parse_context_t* create_parse_context()
{
	auto res = reinterpret_cast<parse_context_t*>( xmalloc( sizeof(parse_context_t) ) );
	return res;
}

void destroy_parse_context( parse_context_t* p )
{
	xfree( p );
}

void initialize_parse_context( parse_context_t* c, tokenize_context_t& t )
{
	c->token_list_ = create_list();
	c->token_current_ = nullptr;
	c->tokenize_context_ = &t;
}

void uninitialize_parse_context( parse_context_t* c )
{
	if ( c->token_list_ != nullptr )
	{
		list_node_t* iter =c->token_list_->head_;
		while( iter != nullptr )
		{
			const auto next =iter->next_;
			token_t* token =reinterpret_cast<token_t*>( iter->value_ );
			destroy_token( token );
			destroy_list_node( iter );
			iter = next;
		}
		destroy_list( c->token_list_ );
	}

	c->token_list_ = nullptr;
	c->token_current_ = nullptr;
}

token_t* read_token( parse_context_t& c )
{
	const auto read = [&c]()
	{
		auto list = create_list_node();
		list->value_ = get_token( *c.tokenize_context_ );
		return list;
	};

	if ( c.token_current_ == nullptr )
	{
		const auto node =read();
		list_append( *c.token_list_, node );
		c.token_current_ = node;
	}
	assert( c.token_current_ != nullptr );
	const auto res = c.token_current_;
	c.token_current_ = c.token_current_->next_;
	return reinterpret_cast<token_t*>( res->value_ );
}

void unread_token( parse_context_t& c, size_t num )
{
	if ( num <= 0 )
	{
		return;
	}

	if ( c.token_current_ == nullptr )
	{
		c.token_current_ = c.token_list_->tail_;
		--num;
	}

	while( num-- > 0 )
	{
		assert( c.token_current_ != nullptr );
		c.token_current_ = c.token_current_->prev_;
	}
}

token_t* prev_token( parse_context_t& c, size_t num )
{
	auto current = c.token_current_;
	if ( current == nullptr )
	{
		current = c.token_list_->tail_;
	}
	while( num-- > 0 )
	{
		assert( current != nullptr );
		current = current->prev_;
	}
	return reinterpret_cast<token_t*>( current->value_ );
}

//=============================================================================
// プリプロセッサ
int query_preprocessor( const char* s )
{
	static const struct
	{
		int				tag_;
		const char*		word_;
	} table[] =
	{
		{ PREPRO_DEFINE,	"define", },
		{ PREPRO_UNDEF,		"undef", },
		{ PREPRO_IF,		"if", },
		{ PREPRO_IFDEF,		"ifdef", },
		{ PREPRO_ENDIF,		"endif", },
		{ PREPRO_ENUM,		"enum", },
		{ -1,				nullptr },
	};

	// 全探索
	for( int i=0; table[i].tag_!=-1; ++i )
	{
		if ( string_equal_igcase( s, table[i].word_ ) )
		{
			return table[i].tag_;
		}
	}
	return PREPRO_UNKNOWN;
}

void initialize_macro_arg( macro_arg_t* ma )
{
	ma->arg_param_ = nullptr;
}

void uninitialize_macro_arg( macro_arg_t* ma )
{
	if ( ma->arg_param_ != nullptr )
	{
		destroy_string( ma->arg_param_ );
		ma->arg_param_ = nullptr;
	}
}

macro_t* create_macro()
{
	auto* const res = reinterpret_cast<macro_t*>( xmalloc( sizeof(macro_t) ) );
	res->name_ = nullptr;
	res->is_ctype_ = false;
	res->param_num_ = 0;
	for ( auto& it : res->params_ )
	{
		it.default_param_ = nullptr;
	}
	res->replacing_ = nullptr;
	return res;
}

void destroy_macro( macro_t* m )
{
	macro_free_content( m );
	xfree( m );
}

void macro_free_content( macro_t* macro )
{
	if ( macro->name_ != nullptr )
	{
		destroy_string( macro->name_ );
		macro->name_ = nullptr;
	}

	if ( macro->replacing_ != nullptr )
	{
		destroy_string( macro->replacing_ );
		macro->replacing_ = nullptr;
	}

	for ( auto& it : macro->params_ )
	{
		if ( it.default_param_ != nullptr )
		{
			destroy_string( it.default_param_ );
			it.default_param_ = nullptr;
		}
	}
	macro->param_num_ = 0;
}

prepro_context_t* create_prepro_context()
{
	auto res = reinterpret_cast<prepro_context_t*>( xmalloc( sizeof(prepro_context_t) ) );
	res->macro_list_ = create_list();
	res->line_ = 0;
	res->out_buffer_ = nullptr;
	res->is_current_region_valid_ = true;
	res->pp_region_idx_ = 0;
	res->pp_region_[0].is_valid_ = true;
	res->pp_region_[0].line_ = true;
	res->enum_next_ = 0;
	return res;
}

void destroy_prepro_context( prepro_context_t* pctx )
{
	assert( pctx != nullptr );
	{
		auto* node = pctx->macro_list_->head_;
		while( node != nullptr )
		{
			auto* macro = reinterpret_cast<macro_t*>( node->value_ );
			destroy_macro( macro );
			node = node->next_;;
		}
	}
	list_free_all( *pctx->macro_list_ );
	destroy_list( pctx->macro_list_ );

	if ( pctx->out_buffer_ != nullptr )
	{
		destroy_string_buffer( pctx->out_buffer_ );
		pctx->out_buffer_ = nullptr;
	}
	xfree( pctx );
}

void prepro_register_default_macros( prepro_context_t* pctx )
{
	{
		auto* const nm = create_macro();
		nm->name_ = create_string( "M_PI" );
		nm->replacing_ = create_string( "3.141592653589793238" );
		prepro_register_macro( pctx, nm );
	}
}

char* prepro_do( const char* src )
{
	auto* pctx = create_prepro_context();
	prepro_register_default_macros( pctx );
	pctx->out_buffer_ = create_string_buffer();

	const char* p = src;
	for ( ; ; )
	{
		const char* const s = p;

		string_buffer_t line;
		initialize_string_buffer( &line );

		{
			bool is_in_multi_line_comment = false;
			for ( ; ; )
			{
				if ( p[0] == '/' && p[1] == '*' )
				{
					is_in_multi_line_comment = true;
					p += 2;
					continue;
				}
				else if ( p[0] == '*' && p[1] == '/' )
				{
					is_in_multi_line_comment = false;
					p += 2;
					continue;
				}
				else if ( p[0] == '\\' && p[1] == '\n' )
				{
					p += 2;
					++pctx->line_;
					continue;
				}
				else if ( p[0] == '\n' )
				{
					if ( !is_in_multi_line_comment )
					{
						break;
					}
					++pctx->line_;
				}
				else if ( p[0] == '\0' )
				{
					break;
				}
				if ( !is_in_multi_line_comment )
				{
					string_buffer_append( &line, p, 1 );
				}
				++p;
			}
		}

		const char* const e = p;

		if ( e > s )
		{
			auto* out_line = prepro_line( pctx, line.buffer_, true );

			if ( out_line != nullptr )
			{
				string_buffer_append( pctx->out_buffer_, out_line );
				destroy_string( out_line );
			}
		}

		uninitialize_string_buffer( &line );

		if ( p[0] == '\0' )
		{
			break;
		}

		string_buffer_append( pctx->out_buffer_, "\n" );

		++p;
		++pctx->line_;
	}

	if ( pctx->pp_region_idx_ > 0 )
	{
		raise_error( "プリプロセス：#if-#endifリージョンが正しく閉じられていません：閉じられていないリージョンの始まり（%d行目）", pctx->pp_region_[0].line_ + 1 );
		return nullptr;
	}

	// 抜き取り
	auto* const res = pctx->out_buffer_->buffer_;
	pctx->out_buffer_->buffer_ = nullptr;
	destroy_prepro_context( pctx );

	return res;
}

char* prepro_line( prepro_context_t* pctx, const char* line, bool enable_preprocessor )
{
	// 最初の空白をスキップ
	while ( line[0] == ' ' || line[0] == '\t' )
	{ ++line; }

	// 普通の行なら展開
	if ( !enable_preprocessor || line[0] != '#' )
	{
		static constexpr int MAX_REPLACE_LOOP = 256;
		int replaced_loop = 0;

		char* res = nullptr;
		char* holded = nullptr;

		// 展開があるならずっとループ
		for ( ; ; )
		{
			bool is_replaced = false;
			res = prepro_line_expand( pctx, line, &is_replaced );
			if ( !is_replaced )
			{
				break;
			}
			if ( holded != nullptr )
			{
				destroy_string( holded );
				holded = nullptr;
			}
			holded = res;
			line = res;

			++replaced_loop;
			if ( replaced_loop > MAX_REPLACE_LOOP )
			{
				raise_error( "マクロの再起展開が上限数（%d）を突破しました、マクロが無限ループしてる可能性があります@@ %d行目", replaced_loop, pctx->line_ + 1 );
				return nullptr;
			}
		}

		if ( holded != nullptr )
		{
			destroy_string( holded );
			holded = nullptr;
		}
		return res;
	}

	// 最初の#はスキップ
	++line;

	tokenize_context_t tokenize_ctx{};
	parse_context_t parse_ctx{};
	initialize_tokenize_context( &tokenize_ctx, line );
	initialize_parse_context( &parse_ctx, tokenize_ctx );

	{
		auto* const st = read_token( parse_ctx );

		if ( st->tag_ != TOKEN_IDENTIFIER )
		{
			raise_error( "プリプロセス：# の後に識別子でないトークンが検出されました@@ %d行目", pctx->line_ + 1 );
			return nullptr;
		}

		const auto pptag = query_preprocessor( st->content_ );
		if ( pptag == PREPRO_UNKNOWN )
		{
			raise_error( "プリプロセス：想定外のプリプロセッサ（%s）@@ %d行目", st->content_, pctx->line_ + 1 );
			return nullptr;
		}

		switch ( pptag )
		{
			case PREPRO_DEFINE:
			{
				if ( pctx->is_current_region_valid_ )
				{
					bool is_ctype = false;
					{
						const auto* const it = read_token( parse_ctx );
						if ( it->tag_ == TOKEN_IDENTIFIER && query_keyword( it->content_ ) == KEYWORD_CTYPE )
						{
							is_ctype = true;
						}
						else
						{
							unread_token( parse_ctx );
						}
					}

					const auto* const nit = read_token( parse_ctx );
					if ( nit->tag_ != TOKEN_IDENTIFIER )
					{
						raise_error( "プリプロセス：define に失敗：識別子名が見つかりません@@ %d行目", pctx->line_ + 1 );
						return nullptr;
					}

					auto* const nmacro = create_macro();
					nmacro->name_ = create_string( nit->content_ );
					nmacro->is_ctype_ = is_ctype;

					const auto* const rt = read_token( parse_ctx );
					if ( rt->tag_ == TOKEN_LPARENTHESIS )
					{
						// パラメータあり
						bool is_break = false;

						for ( ; ; )
						{
							const auto* const ct = read_token( parse_ctx );
							if ( ct->tag_ == TOKEN_EOF )
							{
								raise_error( "プリプロセス：define に失敗：パラメータのパース中に行末に到達しました@@ %d行目", pctx->line_ + 1 );
								return nullptr;
							}
							if ( ct->tag_ == TOKEN_RPARENTHESIS )
							{
								// 何もパラメータがない、おわり
								unread_token( parse_ctx );
								break;
							}
							if ( ct->tag_ != TOKEN_PP_ARG_INDICATOR )
							{
								raise_error( "プリプロセス：define に失敗：パラメータは%%から始まる必要があります@@ %d行目", pctx->line_ + 1 );
								return nullptr;
							}

							const auto* const it = read_token( parse_ctx );
							if ( it->tag_ != TOKEN_INTEGER || it->left_space_ )
							{
								raise_error( "プリプロセス：define に失敗：パラメータ%%のあと、スペースなしで数値を入れる必要があります@@ %d行目", pctx->line_ + 1 );
								return nullptr;
							}

							const auto itidx = atoi( it->content_ );
							if ( itidx != nmacro->param_num_ + 1 )
							{
								raise_error( "プリプロセス：define に失敗：パラメータ%%は順番に定義する必要があります@@ %d行目", pctx->line_ + 1 );
								return nullptr;
							}

							if ( nmacro->param_num_ > MACRO_PARAM_MAX )
							{
								raise_error( "プリプロセス：define に失敗：パラメータ%%の数が多すぎます@@ %d行目", pctx->line_ + 1 );
								return nullptr;
							}

							auto& nparam = nmacro->params_[nmacro->param_num_];
							++nmacro->param_num_;

							const auto* const at = read_token( parse_ctx );
							if ( at->tag_ == TOKEN_ASSIGN )
							{
								// デフォルト値あり
								int parenthesis_depth = 0;
								for ( ; ; )
								{
									const auto* const nt = read_token( parse_ctx );
									if ( parenthesis_depth == 0 && ( nt->tag_ == TOKEN_RPARENTHESIS || nt->tag_ == TOKEN_COMMA ) )
									{
										// ここまで食って終わり
										nparam.default_param_ = create_string( parse_ctx.tokenize_context_->script_ + at->cursor_end_, nt->cursor_begin_ - at->cursor_end_ );

										if ( nt->tag_ == TOKEN_RPARENTHESIS )
										{
											is_break = true;
											unread_token( parse_ctx );
										}
										break;
									}
									if ( nt->tag_ == TOKEN_LPARENTHESIS )
									{
										++parenthesis_depth;
									}
									if ( nt->tag_ == TOKEN_RPARENTHESIS )
									{
										--parenthesis_depth;
									}
									if ( nt->tag_ == TOKEN_EOF )
									{
										raise_error( "プリプロセス：define に失敗：パラメータ%%のデフォルト値を取得中、文末まで到達しました@@ %d行目", pctx->line_ + 1 );
										return nullptr;
									}
								}
							}
							else
							{
								// デフォルト値なし
								if ( at->tag_ == TOKEN_RPARENTHESIS )
								{
									unread_token( parse_ctx );
									break;
								}
								if ( at->tag_ != TOKEN_COMMA )
								{
									raise_error( "プリプロセス：define に失敗：パラメータ%%の定義の後に不明なトークンを読み込みました（%s）@@ %d行目", at->content_, pctx->line_ + 1 );
									return nullptr;
								}
							}

							if ( is_break )
							{ break; }
						}

						// 残り
						const auto* const lrt = read_token( parse_ctx );
						if ( lrt->tag_ != TOKEN_RPARENTHESIS )
						{
							raise_error( "プリプロセス：define に失敗：パラメータ%%の定義の後に不明なトークンを読み込みました（%s）@@ %d行目", lrt->content_, pctx->line_ + 1 );
							return nullptr;
						}
						nmacro->replacing_ = create_string( parse_ctx.tokenize_context_->script_ + lrt->cursor_end_ );
					}
					else
					{
						// パラメータなし
						nmacro->replacing_ = create_string( parse_ctx.tokenize_context_->script_ + rt->cursor_begin_ );
					}

					prepro_register_macro( pctx, nmacro );
				}
				break;
			}
			case PREPRO_UNDEF:
			{
				if ( pctx->is_current_region_valid_ )
				{
					auto* const it = read_token( parse_ctx );
					auto* const et = read_token( parse_ctx );
					if ( it->tag_ != TOKEN_IDENTIFIER || et->tag_ != TOKEN_EOF )
					{
						raise_error( "プリプロセス：undef に失敗：構文が間違っています@@ %d行目", pctx->line_ + 1 );
						return nullptr;
					}
					if ( !prepro_erase_macro( pctx, it->content_ ) )
					{
						raise_error( "プリプロセス：undef に失敗：（%s）が見つかりません@@ %d行目", it->content_, pctx->line_ + 1 );
						return nullptr;
					}
				}
				break;
			}
			case PREPRO_IF:
			{
				bool is_valid = false;
				if ( pctx->is_current_region_valid_ )
				{
					const auto* const rest_top = line + st->cursor_end_;
					auto* rest_pp = prepro_line( pctx, rest_top, false );
					if ( rest_pp == nullptr )
					{
						raise_error( "プリプロセス：if に失敗：式のプリプロセスに失敗@@ %d行目", pctx->line_ + 1 );
						return nullptr;
					}

					tokenize_context_t etoken_ctx{};
					parse_context_t eparse_ctx{};
					initialize_tokenize_context( &etoken_ctx, rest_pp );
					initialize_parse_context( &eparse_ctx, etoken_ctx );

					etoken_ctx.line_ = pctx->line_;// ラインを同期

					auto* east = parse_expression( eparse_ctx );
					if ( east == nullptr )
					{
						raise_error( "プリプロセス：if に失敗：式のパースに失敗@@ %d行目", pctx->line_ + 1 );
						return nullptr;
					}

					auto* ev = evaluate_ast_immediate( east );
					if ( ev == nullptr )
					{
						raise_error( "プリプロセス：if に失敗：式の評価に失敗@@ %d行目", pctx->line_ + 1 );
						return nullptr;
					}

					is_valid = value_calc_boolean( *ev );

					destroy_ast_node( east );
					destroy_value( ev );

					uninitialize_parse_context( &eparse_ctx );
					uninitialize_tokenize_context( &etoken_ctx );
					destroy_string( rest_pp );
				}
				else
				{
					// 何も食わずに判断
					is_valid = false;
				}

				if ( pctx->pp_region_idx_ >= PP_DIRECTIVE_MAX )
				{
					raise_error( "プリプロセス：ifdef に失敗：if ネストが深すぎます@@ %d行目", pctx->line_ + 1 );
					return nullptr;
				}
				++pctx->pp_region_idx_;
				auto& directive = pctx->pp_region_[pctx->pp_region_idx_];
				directive.is_valid_ = ( pctx->is_current_region_valid_ && is_valid );
				directive.line_ = pctx->line_;
				pctx->is_current_region_valid_ = directive.is_valid_;
				break;
			}
			case PREPRO_IFDEF:
			{
				auto* const it = read_token( parse_ctx );
				auto* const et = read_token( parse_ctx );
				if ( it->tag_ != TOKEN_IDENTIFIER || et->tag_ != TOKEN_EOF )
				{
					raise_error( "プリプロセス：ifdef に失敗：構文が間違っています@@ %d行目", pctx->line_ + 1 );
					return nullptr;
				}
				const bool has_macro = ( prepro_find_macro( pctx, it->content_ ) != nullptr );
				if ( pctx->pp_region_idx_ >= PP_DIRECTIVE_MAX )
				{
					raise_error( "プリプロセス：ifdef に失敗：if ネストが深すぎます@@ %d行目", pctx->line_ + 1 );
					return nullptr;
				}
				++pctx->pp_region_idx_;
				auto& directive = pctx->pp_region_[pctx->pp_region_idx_];
				directive.is_valid_ = ( pctx->is_current_region_valid_ && has_macro );
				directive.line_ = pctx->line_;
				pctx->is_current_region_valid_ = directive.is_valid_;
				break;
			}
			case PREPRO_ENDIF:
			{
				auto* const et = read_token( parse_ctx );
				if ( et->tag_ != TOKEN_EOF )
				{
					raise_error( "プリプロセス：endif に失敗：endif後に余計なトークンを検出@@ %d行目", pctx->line_ + 1 );
					return nullptr;
				}
				if ( pctx->pp_region_idx_ <= 0 )
				{
					raise_error( "プリプロセス：endif に失敗：if ネストがされいてません@@ %d行目", pctx->line_ + 1 );
					return nullptr;
				}
				--pctx->pp_region_idx_;
				pctx->is_current_region_valid_ = pctx->pp_region_[pctx->pp_region_idx_].is_valid_;
				break;
			}
			case PREPRO_ENUM:
			{
				if ( pctx->is_current_region_valid_ )
				{
					auto* const it = read_token( parse_ctx );
					if ( it->tag_ != TOKEN_IDENTIFIER )
					{
						raise_error( "プリプロセス：enum に失敗：構文が間違っています@@ %d行目", pctx->line_ + 1 );
						return nullptr;
					}
					auto* const at = read_token( parse_ctx );
					if ( at->tag_ == TOKEN_ASSIGN )
					{
						const auto* const rest_top = line + at->cursor_end_;
						auto* rest_pp = prepro_line( pctx, rest_top, false );
						if ( rest_pp == nullptr )
						{
							raise_error( "プリプロセス：enum に失敗：式のプリプロセスに失敗@@ %d行目", pctx->line_ + 1 );
							return nullptr;
						}

						tokenize_context_t etoken_ctx{};
						parse_context_t eparse_ctx{};
						initialize_tokenize_context( &etoken_ctx, rest_pp );
						initialize_parse_context( &eparse_ctx, etoken_ctx );

						etoken_ctx.line_ = pctx->line_;// ラインを同期

						auto* east = parse_expression( eparse_ctx );
						if ( east == nullptr )
						{
							raise_error( "プリプロセス：enum に失敗：式のパースに失敗@@ %d行目", pctx->line_ + 1 );
							return nullptr;
						}

						auto* ev = evaluate_ast_immediate( east );
						if ( ev == nullptr )
						{
							raise_error( "プリプロセス：enum に失敗：式の評価に失敗@@ %d行目", pctx->line_ + 1 );
							return nullptr;
						}

						value_isolate( *ev );
						if ( value_get_primitive_tag( *ev ) != VALUE_INT )
						{
							raise_error( "プリプロセス：enum に失敗：評価後の値がintではありません@@ %d行目", pctx->line_ + 1 );
							return nullptr;
						}

						pctx->enum_next_ = value_calc_int( *ev );

						destroy_ast_node( east );
						destroy_value( ev );

						uninitialize_parse_context( &eparse_ctx );
						uninitialize_tokenize_context( &etoken_ctx );
						destroy_string( rest_pp );
					}
					else
					{
						if ( at->tag_ != TOKEN_EOF )
						{
							raise_error( "プリプロセス：enum に失敗：enum後に余計なトークンを検出@@ %d行目", pctx->line_ + 1 );
							return nullptr;
						}
					}

					// 登録
					auto* const nm = create_macro();
					nm->name_ = create_string( it->content_ );
					nm->replacing_ = create_string_from( pctx->enum_next_ );
					prepro_register_macro( pctx, nm );
					++pctx->enum_next_;
				}
				break;
			}
			default: assert( false ); break;
		}
	}

	uninitialize_parse_context( &parse_ctx );
	uninitialize_tokenize_context( &tokenize_ctx );

	return create_string( "" );
}

char* prepro_line_expand( prepro_context_t* pctx, const char* line, bool* out_is_replaced )
{
	// このディレクティブが無効なら何もしない
	if ( !pctx->is_current_region_valid_ )
	{
		if ( out_is_replaced != nullptr )
		{
			*out_is_replaced = false;
		}
		return create_string( "" );
	}

	// マクロ展開
	auto* const sb = create_string_buffer();

	tokenize_context_t tokenize_ctx{};
	parse_context_t parse_ctx{};
	initialize_tokenize_context( &tokenize_ctx, line );
	initialize_parse_context( &parse_ctx, tokenize_ctx );

	bool is_replaced = false;
	{
		const token_t* prev = nullptr;

		for ( ; ; )
		{
			const auto* const st = read_token( parse_ctx );

			if ( prev != nullptr )
			{
				const auto len = st->cursor_begin_ - prev->cursor_end_;
				if ( len > 0 )
				{
					string_buffer_append( sb, line + prev->cursor_end_, len );
				}
			}
			prev = st;

			if ( st->tag_ == TOKEN_EOF )
			{
				break;
			}

			if ( st->tag_ == TOKEN_IDENTIFIER )
			{
				const auto* const macro = prepro_find_macro( pctx, st->content_ );
				if ( macro != nullptr )
				{
					if ( macro->param_num_ > 0 )
					{
						// パラメータ取得
						macro_arg_t marg[ MACRO_PARAM_MAX ];
						int marg_num = 0;
						for ( auto& it : marg )
						{
							initialize_macro_arg( &it );
						}

						if ( macro->is_ctype_ )
						{
							const auto* const lt = read_token( parse_ctx );
							if ( lt->tag_ != TOKEN_LPARENTHESIS )
							{
								raise_error( "プリプロセス：マクロ展開（%s）：ctypeと定義されていますが、引数が括弧で始まっていません@@ %d行目", macro->name_, pctx->line_ + 1 );
								return nullptr;
							}
						}
						else
						{
							const auto* const nst = read_token( parse_ctx );
							unread_token( parse_ctx );

							if ( !nst->left_space_ && ( nst->tag_ != TOKEN_EOS && nst->tag_ != TOKEN_EOF ) )
							{
								raise_error( "プリプロセス：マクロ展開（%s）：ctypeと定義されていないマクロ展開において、マクロの後にスペースが存在しません@@ %d行目", macro->name_, pctx->line_ + 1 );
								return nullptr;
							}
						}

						for ( ; ; )
						{
							const auto* const nst = read_token( parse_ctx );
							unread_token( parse_ctx );

							bool is_break = false;
							int parenthesis_depth = 0;

							for ( ; ; )
							{
								const auto* const nt = read_token( parse_ctx );
								if ( nt->tag_ == TOKEN_EOS || nt->tag_ == TOKEN_EOF )
								{
									if ( parenthesis_depth > 0 || macro->is_ctype_ )
									{
										raise_error( "プリプロセス：マクロ展開（%s）に失敗：パラメータ読み取り中に、予期しないステートメントの終わり（:）か行末に到達しました@@ %d行目", macro->name_, pctx->line_ + 1 );
										return nullptr;
									}

									// ここまで食う
									if ( marg_num >= macro->param_num_ )
									{
										raise_error( "プリプロセス：マクロ展開（%s）に失敗：与えられて引数が多すぎます@@ %d行目", macro->name_, pctx->line_ + 1 );
										return nullptr;
									}
									auto& ma = marg[ marg_num ];
									++marg_num;

									// 引数省略
									if ( nst != nt )
									{
										ma.arg_param_ = create_string( line + nst->cursor_begin_, nt->cursor_begin_ - nst->cursor_begin_ );
									}

									unread_token( parse_ctx );
									is_break = true;
									break;
								}
								const bool is_rp_end = ( macro->is_ctype_ && nt->tag_ == TOKEN_RPARENTHESIS );
								if ( parenthesis_depth == 0 && ( is_rp_end || nt->tag_ == TOKEN_COMMA ) )
								{
									// ここまで食う
									if ( marg_num >= macro->param_num_ )
									{
										raise_error( "プリプロセス：マクロ展開（%s）に失敗：与えられて引数が多すぎます@@ %d行目", macro->name_, pctx->line_ + 1 );
										return nullptr;
									}
									auto& ma = marg[ marg_num ];
									++marg_num;

									// 引数省略
									if ( nst != nt )
									{
										ma.arg_param_ = create_string( line + nst->cursor_begin_, nt->cursor_begin_ - nst->cursor_begin_ );
									}

									if ( is_rp_end )
									{
										unread_token( parse_ctx );// 食いすぎなので戻す
										is_break = true;
									}
									break;
								}
								if ( nt->tag_ == TOKEN_LPARENTHESIS )
								{
									++parenthesis_depth;
								}
								if ( nt->tag_ == TOKEN_RPARENTHESIS )
								{
									--parenthesis_depth;
								}
								if ( nt->tag_ == TOKEN_EOF )
								{
									if ( macro->is_ctype_ )
									{
										raise_error( "プリプロセス：マクロ展開（%s）に失敗：ctypeで定義されていますが、括弧の終わりがみつかりません@@ %d行目", macro->name_, pctx->line_ + 1 );
										return nullptr;
									}

									// ここまで食う
									if ( marg_num >= macro->param_num_ )
									{
										raise_error( "プリプロセス：マクロ展開（%s）に失敗：与えられて引数が多すぎます@@ %d行目", macro->name_, pctx->line_ + 1 );
										return nullptr;
									}
									auto& ma = marg[ marg_num ];
									++marg_num;

									// 引数省略
									if ( nst != nt )
									{
										ma.arg_param_ = create_string( line + nst->cursor_begin_ );
									}

									is_break = true;
									break;
								}
							}

							if ( is_break )
							{ break; }
						}

						// 引数閉じ終わり？
						if ( macro->is_ctype_ )
						{
							const auto* const rt = read_token( parse_ctx );
							if ( rt->tag_ != TOKEN_RPARENTHESIS )
							{
								raise_error( "プリプロセス：マクロ展開（%s）に失敗：ctypeと定義されていますが、引数が括弧で終わっていません@@ %d行目", macro->name_, pctx->line_ + 1 );
								return nullptr;
							}
						}

						// 次の取得用にずらしておく
						prev = read_token( parse_ctx );
						unread_token( parse_ctx );

						// 省略チェック
						for ( int i = 0; i < macro->param_num_; ++i )
						{
							const auto& mp = macro->params_[i];
							const auto& ma = marg[i];
							if ( ma.arg_param_ == nullptr && mp.default_param_ == nullptr )
							{
								raise_error( "プリプロセス：マクロ展開（%s）に失敗：%d番目の引数は省略できません@@ %d行目", macro->name_, i + 1, pctx->line_ + 1 );
								return nullptr;
							}
						}

						// 置き換え
						{
							parse_context_t lpctx{};
							tokenize_context_t ltctx{};
							initialize_tokenize_context( &ltctx, macro->replacing_ );
							initialize_parse_context( &lpctx, ltctx );

							const token_t* iprev = nullptr;
							for ( ; ; )
							{
								const auto* const ist = read_token( lpctx );
								if ( ist->tag_ == TOKEN_EOF )
								{
									break;
								}

								if ( ist->tag_ == TOKEN_PP_ARG_INDICATOR )
								{
									const auto* const it = read_token( lpctx );
									if ( it->tag_ != TOKEN_INTEGER || it->left_space_ )
									{
										raise_error( "プリプロセス：マクロ展開（%s）に失敗：展開後のパラメータ%%のあと、スペースなしで数値を入れる必要があります@@ %d行目", macro->name_, pctx->line_ + 1 );
										return nullptr;
									}

									const auto itidx = atoi( it->content_ ) - 1;
									if ( itidx < 0 || itidx > macro->param_num_ )
									{
										raise_error( "プリプロセス：マクロ展開（%s）に失敗：パラメータ%が存在しません@@ %d行目", macro->name_, pctx->line_ + 1 );
										return nullptr;
									}
									const auto& mp = macro->params_[itidx];
									const auto& ma = marg[itidx];

									string_buffer_append( sb, ( ma.arg_param_ == nullptr ? mp.default_param_ : ma.arg_param_ ) );
									iprev = it;
								}
								else
								{
									if ( iprev != nullptr )
									{
										string_buffer_append( sb, macro->replacing_ + iprev->cursor_end_, ist->cursor_begin_ - iprev->cursor_end_ );
									}
									string_buffer_append( sb, macro->replacing_ + ist->cursor_begin_, ist->cursor_end_ - ist->cursor_begin_ );
									iprev = ist;
								}
							}

							if ( iprev != nullptr )
							{
								string_buffer_append( sb, macro->replacing_ + iprev->cursor_end_ );
							}

							uninitialize_parse_context( &lpctx );
							uninitialize_tokenize_context( &ltctx );
						}

						for ( auto& it : marg )
						{
							uninitialize_macro_arg( &it );
						}
					}
					else
					{
						// 単純置き換え
						string_buffer_append( sb, macro->replacing_ );
					}

					is_replaced = true;
					continue;
				}
			}

			string_buffer_append( sb, line + st->cursor_begin_, st->cursor_end_ - st->cursor_begin_ );
			continue;
		}
	}

	uninitialize_parse_context( &parse_ctx );
	uninitialize_tokenize_context( &tokenize_ctx );

	// 抜き取り
	auto* res = sb->buffer_;
	sb->buffer_ = nullptr;
	destroy_string_buffer( sb );

	if ( out_is_replaced != nullptr )
	{
		*out_is_replaced = is_replaced;
	}
	return res;
}

macro_t* prepro_find_macro( prepro_context_t* pctx, const char* name )
{
	auto* node = pctx->macro_list_->head_;
	while( node != nullptr )
	{
		auto* const macro = reinterpret_cast<macro_t*>( node->value_ );
		if ( string_equal_igcase( macro->name_, name ) )
		{
			return macro;
		}
		node = node->next_;
	}

	return nullptr;
}

bool prepro_register_macro( prepro_context_t* pctx, macro_t* macro )
{
	const auto* d = prepro_find_macro( pctx, macro->name_ );

	if ( d != nullptr )
	{
		raise_error( "プリプロセス：マクロが再定義されました（%s）", macro->name_ );
		return false;
	}

	auto* const lnode = create_list_node();
	lnode->value_ = macro;

	list_append( *pctx->macro_list_, lnode );

	return true;
}

bool prepro_erase_macro( prepro_context_t* pctx, const char* name )
{
	list_node_t* found_node = nullptr;
	auto* node = pctx->macro_list_->head_;
	while( node != nullptr )
	{
		auto* const macro = reinterpret_cast<macro_t*>( node->value_ );
		if ( string_equal_igcase( macro->name_, name ) )
		{
			found_node = node;
			break;
		}
		node = node->next_;
	}

	if ( found_node == nullptr )
	{ return false; }

	list_erase( *pctx->macro_list_, found_node );
	return true;
}

//=============================================================================
// 抽象構文木
ast_node_t* create_ast_node( node_tag tag, ast_node_t* left, ast_node_t* right )
{
	auto res = reinterpret_cast<ast_node_t*>( xmalloc( sizeof(ast_node_t) ) );
	res->tag_ = tag;
	res->token_ = nullptr;
	res->left_ = left;
	res->right_ = right;
	res->flag_ = 0;
	return res;
}

ast_node_t* create_ast_node( node_tag tag, token_t* token, ast_node_t* left )
{
	auto res = reinterpret_cast<ast_node_t*>( xmalloc( sizeof(ast_node_t) ) );
	res->tag_ = tag;
	res->token_ = token;
	res->left_ = left;
	res->right_ = nullptr;
	res->flag_ = 0;
	return res;
}

void destroy_ast_node( ast_node_t* node )
{
	assert( node != nullptr );
	if ( node->left_ != nullptr )
	{ destroy_ast_node( node->left_ ); }
	if ( node->right_ != nullptr )
	{ destroy_ast_node( node->right_ ); }
	xfree( node );
}

bool is_eos_like_token( token_tag tag )
{
	return ( tag==TOKEN_EOF || tag==TOKEN_EOL || tag==TOKEN_EOS || tag==TOKEN_RBRACE );
}

list_t* parse_script( parse_context_t& c )
{
	auto res = create_list();

	for( ; ; )
	{
		auto statement = parse_statement( c );
		if ( statement == nullptr )
		{ break; }

		auto node = create_list_node();
		node->value_ = statement;
		list_append( *res, node );
	}

	{
		const auto token = read_token( c );
		if ( token->tag_ != TOKEN_EOF )
		{
			raise_error( "スクリプトを最後まで正しくパースできませんでした@@ %d行目", token->appear_line_ );
		}
	}

	return res;
}

void destroy_ast( list_t* ast )
{
	auto node = ast->head_;
	while( node != nullptr )
	{
		const auto ast_node = reinterpret_cast<ast_node_t*>( node->value_ );
		destroy_ast_node( ast_node );
		node = node->next_;
	}

	list_free_all( *ast );
	destroy_list( ast );
}

ast_node_t* parse_statement( parse_context_t& c )
{
	// 何もない？
	{
		const auto token = read_token( c );
		if ( token->tag_ == TOKEN_EOF )
		{ return nullptr; }
		if ( is_eos_like_token( token->tag_ ) )
		{ return create_ast_node( NODE_EMPTY ); }
		unread_token( c );
	}

	ast_node_t* statement = nullptr;

	// ラベルを試す
	if ( statement == nullptr )
	{
		statement = parse_label_safe( c );
	}

	// 制御構文を試す
	if ( statement == nullptr )
	{
		statement = parse_control_safe( c );
	}

	// コマンドを試す
	if ( statement == nullptr )
	{
		statement = parse_command_safe( c );
	}

	// それ以外は全て代入
	if ( statement == nullptr )
	{
		statement = parse_assign_safe( c );
	}

	// ここまで来て何もないなら、パース不能
	if ( statement == nullptr )
	{
		const auto token = read_token( c );
		raise_error( "ステートメントが解析不能です@@ %d行目", token->appear_line_ );
	}

	// 最後の改行チェック
	{
		const auto token = read_token( c );
		if ( !is_eos_like_token( token->tag_ ) )
		{
			raise_error( "ステートメントをすべて正しく解析できませんでした@@ %d行目", token->appear_line_ );
		}
	}

	return statement;
}

ast_node_t* parse_label_safe( parse_context_t& c )
{
	const auto token = read_token( c );
	if ( token->tag_ != TOKEN_OP_MUL )
	{
		unread_token( c );
		return nullptr;
	}

	const auto ident = read_token( c );
	if ( ident->tag_ != TOKEN_IDENTIFIER )
	{
		unread_token( c, 2 );
		return nullptr;
	}

	return create_ast_node( NODE_LABEL, ident );
}

ast_node_t* parse_control_safe( parse_context_t& c )
{
	const auto ident = read_token( c );
	if ( ident->tag_ != TOKEN_IDENTIFIER )
	{
		unread_token( c );
		return nullptr;
	}

	const auto keyword = query_keyword( ident->content_ );
	if ( keyword < 0 )
	{
		unread_token( c );
		return nullptr;
	}

	switch( keyword )
	{
		case KEYWORD_END:
			return create_ast_node( NODE_END );
		case KEYWORD_RETURN:
		{
			const auto next = read_token( c );
			unread_token( c );

			ast_node_t* expr =nullptr;
			if ( !is_eos_like_token( next->tag_ ) )
			{
				expr = parse_expression( c );
			}
			return create_ast_node( NODE_RETURN, expr );
		}
		case KEYWORD_GOTO:
		case KEYWORD_GOSUB:
		{
			const auto label = parse_label_safe( c );
			if ( label == nullptr )
			{
				raise_error( "gotoまたはgosubにはラベルの指定が必須です@@ %d行目", ident->appear_line_ );
			}
			return create_ast_node( keyword==KEYWORD_GOTO ? NODE_GOTO : NODE_GOSUB, label );
		}
		case KEYWORD_REPEAT:
		{
			const auto next = read_token( c );
			unread_token( c );

			ast_node_t* expr =nullptr;
			if ( !is_eos_like_token( next->tag_ ) )
			{
				expr = parse_expression( c );
			}
			return create_ast_node( NODE_REPEAT, expr );
		}
		case KEYWORD_LOOP:
			return create_ast_node( NODE_LOOP );
		case KEYWORD_CONTINUE:
			return create_ast_node( NODE_CONTINUE );
		case KEYWORD_BREAK:
			return create_ast_node( NODE_BREAK );
		case KEYWORD_IF:
		{
			const auto expr = parse_expression( c );

			const auto next = read_token( c );

			const auto is_else_token = []( const token_t* n )
			{
				if ( n->tag_==TOKEN_IDENTIFIER && query_keyword(n->content_)==KEYWORD_ELSE )
				{ return true; }
				return false;
			};

			bool repair_token =false;

			ast_node_t* true_statements = create_ast_node( NODE_BLOCK_STATEMENTS );
			ast_node_t* false_statements = nullptr;
			if ( next->tag_ == TOKEN_LBRACE )
			{
				for( ; ; )
				{
					const auto pp = prev_token( c );
					if ( pp->tag_ == TOKEN_RBRACE )
					{
						// RBRACEは食われてた
						break;
					}
					const auto statement = parse_statement( c );
					if ( statement == nullptr )
					{
						raise_error( "if文の解析中、解析できないステートメントに到達しました@@ %d行目、%d行目から", pp->appear_line_, ident->appear_line_ );
					}
					true_statements = create_ast_node( NODE_BLOCK_STATEMENTS, true_statements, statement );
				}
			}
			else
			{
				unread_token( c );
				{
					const auto nn = read_token( c );
					if ( nn->tag_ != TOKEN_EOS )
					{
						raise_error( "if文の解析中：ifの条件式の後は { か : しか置けません@@ %d行目", nn->appear_line_ );
					}
				}

				for( ; ; )
				{
					const auto pp = prev_token( c );
					if ( pp->tag_==TOKEN_EOL || pp->tag_==TOKEN_EOF )
					{
						// EOLまたはEOFは食われてた
						repair_token =true;
						break;
					}
					const auto nn = read_token( c );
					unread_token( c );
					if ( is_else_token( nn ) )
					{ break; }

					const auto statement = parse_statement( c );
					if ( statement == nullptr )
					{
						raise_error( "if文の解析中、解析できないステートメントに到達しました@@ %d行目、%d行目から", nn->appear_line_, ident->appear_line_ );
					}
					true_statements = create_ast_node( NODE_BLOCK_STATEMENTS, true_statements, statement );
				}
			}

			// elseはあるか？
			const auto nn = read_token( c );
			if ( is_else_token( nn ) )
			{
				repair_token = false;
				false_statements = create_ast_node( NODE_BLOCK_STATEMENTS );

				const auto nextf = read_token( c );
				if ( nextf->tag_ == TOKEN_LBRACE )
				{
					for( ; ; )
					{
						const auto ppf = prev_token( c );
						if ( ppf->tag_ == TOKEN_RBRACE )
						{
							// RBRACEは食われてた
							break;
						}
						const auto statement = parse_statement( c );
						if ( statement == nullptr )
						{
							raise_error( "ifのelse文の解析中、解析できないステートメントに到達しました@@ %d行目、%d行目から", nn->appear_line_, ident->appear_line_ );
						}
						false_statements = create_ast_node( NODE_BLOCK_STATEMENTS, false_statements, statement );
					}
				}
				else
				{
					unread_token( c );
					{
						const auto nnf = read_token( c );
						if ( nnf->tag_ != TOKEN_EOS )
						{
							raise_error( "ifのelse文の解析中：elseの後は { か : しか置けません@@ %d行目", nnf->appear_line_ );
						}
					}

					for( ; ; )
					{
						const auto pp = prev_token( c );
						if ( pp->tag_==TOKEN_EOL || pp->tag_==TOKEN_EOF )
						{
							// EOLまたはEOFは食われてた
							repair_token = true;
							break;
						}
						const auto nnf = read_token( c );
						unread_token( c );
						if ( is_else_token( nnf ) )
						{ break; }

						const auto statement = parse_statement( c );
						if ( statement == nullptr )
						{
							raise_error( "ifのelse文の解析中、解析できないステートメントに到達しました@@ %d行目、%d行目から", nnf->appear_line_, ident->appear_line_ );
						}
						false_statements = create_ast_node( NODE_BLOCK_STATEMENTS, false_statements, statement );
					}
				}
			}
			else
			{
				unread_token( c );
			}

			// EOLを食いすぎてしまった場合用
			if ( repair_token )
			{
				unread_token( c );
			}

			ast_node_t* dispatcher = create_ast_node( NODE_IF_DISPATCHER, true_statements, false_statements );
			return create_ast_node( NODE_IF, expr, dispatcher );
		}
		case KEYWORD_ELSE:
			raise_error( "ハンドルされないelseを検出しました@@ %d行目", ident->appear_line_ );
			break;
	}

	unread_token( c );
	return nullptr;
}

ast_node_t* parse_command_safe( parse_context_t& c )
{
	const auto ident = read_token( c );
	if ( ident->tag_ != TOKEN_IDENTIFIER )
	{
		unread_token( c );
		return nullptr;
	}

	const auto next = read_token( c );

	bool is_not_command = false;
	switch( next->tag_ )
	{
	case TOKEN_ASSIGN:
	case TOKEN_ADD_ASSIGN:
	case TOKEN_SUB_ASSIGN:
	case TOKEN_MUL_ASSIGN:
	case TOKEN_DIV_ASSIGN:
	case TOKEN_MOD_ASSIGN:
	case TOKEN_BOR_ASSIGN:
	case TOKEN_BAND_ASSIGN:
	case TOKEN_BXOR_ASSIGN:
		is_not_command = true;
		break;
	default: break;
	}

	// "("チェック
	if ( !ident->right_space_ && next->tag_==TOKEN_LPARENTHESIS )
	{ is_not_command = true; }

	if ( is_not_command )
	{
		unread_token( c, 2 );
		return nullptr;
	}

	// あるなら引数の解析
	ast_node_t* args = nullptr;
	if ( !is_eos_like_token( next->tag_ ) )
	{
		unread_token( c );
		args = parse_arguments( c );
	}
	else
	{
		unread_token( c );
	}

	const auto command = create_ast_node( NODE_COMMAND, ident, args );
 	return command;
}

ast_node_t* parse_arguments( parse_context_t& c )
{
	auto arg = parse_expression( c );
	auto res = create_ast_node( NODE_ARGUMENTS, arg );
	auto args = res;

	for( ; ; )
	{
		const auto token = read_token( c );
		if ( token->tag_ != TOKEN_COMMA )
		{
			unread_token( c );
			break;
		}

		auto next =parse_expression( c );
		args->right_ = create_ast_node( NODE_ARGUMENTS, next );
		args = args->right_;
	}
	return res;
}

ast_node_t* parse_assign_safe( parse_context_t& c )
{
	auto variable = parse_variable_safe( c );
	if ( variable == nullptr )
	{ return nullptr; }

	const auto next = read_token( c );

	int node =-1;
	switch( next->tag_ )
	{
	case TOKEN_ASSIGN:			node = NODE_ASSIGN; break;
	case TOKEN_ADD_ASSIGN:		node = NODE_ADD_ASSIGN; break;
	case TOKEN_SUB_ASSIGN:		node = NODE_SUB_ASSIGN; break;
	case TOKEN_MUL_ASSIGN:		node = NODE_MUL_ASSIGN; break;
	case TOKEN_DIV_ASSIGN:		node = NODE_DIV_ASSIGN; break;
	case TOKEN_MOD_ASSIGN:		node = NODE_MOD_ASSIGN; break;
	case TOKEN_BOR_ASSIGN:		node = NODE_BOR_ASSIGN; break;
	case TOKEN_BAND_ASSIGN:		node = NODE_BAND_ASSIGN; break;
	case TOKEN_BXOR_ASSIGN:		node = NODE_BXOR_ASSIGN; break;
	default:
		raise_error( "代入 : =が必要です@@ %d行目", next->appear_line_ );
		break;
	}

	if ( node == -1 )
	{ return nullptr; }

	auto expr = parse_expression( c );
	auto assign = create_ast_node( static_cast<node_tag>( node ), variable, expr );
	return assign;
}

ast_node_t* parse_variable_safe( parse_context_t& c )
{
	const auto ident = read_token( c );
	if ( ident->tag_ != TOKEN_IDENTIFIER )
	{
		unread_token( c );
		return nullptr;
	}

	const auto next = read_token( c );
	if ( next->tag_ != TOKEN_LPARENTHESIS )
	{
		unread_token( c );
		return create_ast_node( NODE_VARIABLE, ident );
	}

	const auto idx = parse_expression( c );
	const auto nn = read_token( c );
	if ( nn->tag_ != TOKEN_RPARENTHESIS )
	{
		// 多そうなので個別対処
		if ( nn->tag_ == TOKEN_COMMA )
		{ raise_error( "配列変数 : 二次元以上の配列はサポートしていません@@ %d行目", nn->appear_line_ ); }
		raise_error( "配列変数 : 括弧が正しく閉じられていません@@ %d行目", nn->appear_line_ );
	}

	return create_ast_node( NODE_VARIABLE, ident, idx );
}

ast_node_t* parse_expression( parse_context_t& c )
{
	// ただの関数転送
	return parse_borand( c );
}

ast_node_t* parse_borand( parse_context_t& c )
{
	auto node =parse_eqneq(c);

	for( ; ; )
	{
		bool is_continue = true;
		const auto token = read_token( c );
		switch( token->tag_ )
		{
			case TOKEN_OP_BOR:
			case TOKEN_OP_BAND:
			case TOKEN_OP_BXOR:
			{
				auto r = parse_eqneq( c );
				if ( r == nullptr )
				{ raise_error( "|,&の演算子で、右項の解析が出来ません@@ %d行目", token->appear_line_ ); }

				switch( token->tag_ )
				{
					case TOKEN_OP_BOR:	node = create_ast_node( NODE_BOR, node, r ); break;
					case TOKEN_OP_BAND:	node = create_ast_node( NODE_BAND, node, r ); break;
					case TOKEN_OP_BXOR:	node = create_ast_node( NODE_BXOR, node, r ); break;
					default: assert( false ); break;
				}
				break;
			}
			default:
				is_continue = false;
				break;
		}
		if ( !is_continue )
		{
			unread_token( c );
			break;
		}
	}
	return node;
}

ast_node_t* parse_eqneq( parse_context_t& c )
{
	auto node =parse_gtlt(c);

	for( ; ; )
	{
		bool is_continue = true;
		const auto token = read_token( c );
		switch( token->tag_ )
		{
			case TOKEN_OP_EQ:
			case TOKEN_OP_NEQ:
			case TOKEN_ASSIGN:
			{
				auto r = parse_gtlt( c );
				if ( r == nullptr )
				{ raise_error( "==,!=の演算子で、右項の解析が出来ません@@ %d行目", token->appear_line_ ); }

				switch( token->tag_ )
				{
					case TOKEN_OP_EQ:
					case TOKEN_ASSIGN:
						node = create_ast_node( NODE_EQ, node, r );
						break;
					case TOKEN_OP_NEQ:	node = create_ast_node( NODE_NEQ, node, r ); break;
					default: assert( false ); break;
				}
				break;
			}
			default:
				is_continue = false;
				break;
		}
		if ( !is_continue )
		{
			unread_token( c );
			break;
		}
	}
	return node;
}

ast_node_t* parse_gtlt( parse_context_t& c )
{
	auto node =parse_addsub(c);

	for( ; ; )
	{
		bool is_continue = true;
		const auto token = read_token( c );
		switch( token->tag_ )
		{
			case TOKEN_OP_GT:
			case TOKEN_OP_GTOE:
			case TOKEN_OP_LT:
			case TOKEN_OP_LTOE:
			{
				auto r = parse_addsub( c );
				if ( r == nullptr )
				{ raise_error( ">,>=,<,<=の演算子で、右項の解析が出来ません@@ %d行目", token->appear_line_ ); }

				switch( token->tag_ )
				{
					case TOKEN_OP_GT:	node = create_ast_node( NODE_GT, node, r ); break;
					case TOKEN_OP_GTOE:	node = create_ast_node( NODE_GTOE, node, r ); break;
					case TOKEN_OP_LT:	node = create_ast_node( NODE_LT, node, r ); break;
					case TOKEN_OP_LTOE:	node = create_ast_node( NODE_LTOE, node, r ); break;
					default: assert( false ); break;
				}
				break;
			}
			default:
				is_continue = false;
				break;
		}
		if ( !is_continue )
		{
			unread_token( c );
			break;
		}
	}
	return node;
}

ast_node_t* parse_addsub( parse_context_t& c )
{
	auto node =parse_muldivmod(c);

	for( ; ; )
	{
		bool is_continue = true;
		const auto token = read_token( c );
		switch( token->tag_ )
		{
			case TOKEN_OP_ADD:
			case TOKEN_OP_SUB:
			{
				auto r = parse_muldivmod( c );
				if ( r == nullptr )
				{ raise_error( "+-の演算子で、右項の解析が出来ません@@ %d行目", token->appear_line_ ); }

				switch( token->tag_ )
				{
					case TOKEN_OP_ADD:	node = create_ast_node( NODE_ADD, node, r ); break;
					case TOKEN_OP_SUB:	node = create_ast_node( NODE_SUB, node, r ); break;
					default: assert( false ); break;
				}
				break;
			}
			default:
				is_continue = false;
				break;
		}
		if ( !is_continue )
		{
			unread_token( c );
			break;
		}
	}
	return node;
}

ast_node_t* parse_muldivmod( parse_context_t& c )
{
	auto node =parse_term(c);

	for( ; ; )
	{
		bool is_continue = true;
		const auto token = read_token( c );
		switch( token->tag_ )
		{
			case TOKEN_OP_MUL:
			case TOKEN_OP_DIV:
			case TOKEN_OP_MOD:
			{
				auto r = parse_term( c );
				if ( r == nullptr )
				{ raise_error( "*/\\の演算子で、右項の解析が出来ません@@ %d行目", token->appear_line_ ); }

				switch( token->tag_ )
				{
					case TOKEN_OP_MUL:	node = create_ast_node( NODE_MUL, node, r ); break;
					case TOKEN_OP_DIV:	node = create_ast_node( NODE_DIV, node, r ); break;
					case TOKEN_OP_MOD:	node = create_ast_node( NODE_MOD, node, r ); break;
					default: assert( false ); break;
				}
				break;
			}
			default:
				is_continue = false;
				break;
		}
		if ( !is_continue )
		{
			unread_token( c );
			break;
		}
	}
	return node;
}

ast_node_t* parse_term( parse_context_t& c )
{
	const auto token = read_token( c );
	switch( token->tag_ )
	{
		case TOKEN_OP_SUB:		return create_ast_node( NODE_UNARY_MINUS, parse_primitive( c ) );
		default: break;
	}

	unread_token( c );
	return parse_primitive( c );
}

ast_node_t* parse_primitive( parse_context_t& c )
{
	const auto token = read_token( c );
	switch( token->tag_ )
	{
		case TOKEN_LPARENTHESIS:
		{
			const auto node = parse_expression( c );
			const auto next = read_token( c );
			if ( next->tag_ != TOKEN_RPARENTHESIS )
			{
				raise_error( "括弧が閉じられていません@@ %d行目", token->appear_line_ );
			}
			return create_ast_node( NODE_EXPRESSION, node );
		}

		case TOKEN_INTEGER:
		case TOKEN_REAL:
		case TOKEN_STRING:
			return create_ast_node( NODE_PRIMITIVE_VALUE, token );

		case TOKEN_OP_MUL:
		{
			unread_token( c );
			const auto label = parse_label_safe( c );
			if ( label == nullptr )
			{
				raise_error( "ラベルが正しく解析できませんでした@@ %d行目", token->appear_line_ );
			}
			raise_error( "式中にラベル型を使うことはできません@@ %d行目", token->appear_line_ );
			return label;
		}

		case TOKEN_IDENTIFIER:
		{
			unread_token( c );
			const auto expr = parse_identifier_expression( c );
			if ( expr == nullptr )
			{
				raise_error( "関数または変数を正しく解析できませんでした@@ %d行目", token->appear_line_ );
			}
			return expr;
		}
		default: break;
	}

	raise_error( "プリミティブな値を取得できません@@ %d行目[%s]", token->appear_line_, token->content_ );
	return nullptr;
}

ast_node_t* parse_identifier_expression( parse_context_t& c )
{
	const auto ident = read_token( c );
	if ( ident->tag_ != TOKEN_IDENTIFIER )
	{
		unread_token( c );
		return nullptr;
	}

	const auto next = read_token( c );
	if ( next->tag_ != TOKEN_LPARENTHESIS )
	{
		unread_token( c );
		return create_ast_node( NODE_IDENTIFIER_EXPR, ident );
	}

	// 引数なしの即閉じ？
	{
		const auto nn = read_token( c );
		if ( nn->tag_ == TOKEN_RPARENTHESIS )
		{
			return create_ast_node( NODE_IDENTIFIER_EXPR, ident, create_ast_node( NODE_ARGUMENTS ) );
		}
		unread_token( c );
	}

	// 引数あり
	const auto idx = parse_arguments( c );
	const auto nn = read_token( c );
	if ( nn->tag_ != TOKEN_RPARENTHESIS )
	{
		raise_error( "関数または配列変数 : 括弧が正しく閉じられていません@@ %d行目", nn->appear_line_ );
	}

	return create_ast_node( NODE_IDENTIFIER_EXPR, ident, idx );
}

//=============================================================================
// 変数
variable_t* create_variable( const char* name )
{
	const auto res = reinterpret_cast<variable_t*>( xmalloc( sizeof(variable_t) ) );
	res->name_ = create_string( name );
	res->type_ = VALUE_NONE;
	res->granule_size_  = 0;
	res->length_ = 0;
	res->data_ = nullptr;
	res->data_size_ = 0;
	prepare_variable( res, VALUE_INT, 64, 16 );
	return res;
}

void destroy_variable( variable_t* v )
{
	xfree( v->name_ );
	xfree( v->data_ );
	v->data_size_ = 0;
	xfree( v );
}

void prepare_variable( variable_t* v, value_tag type, int granule_size, int length )
{
	if ( v->data_ != nullptr )
	{
		xfree( v->data_ );
		v->data_ = nullptr;
		v->data_size_ = 0;
	}

	v->type_ = type;
	v->granule_size_ = granule_size;
	v->length_ = length;

	size_t areasize = 0;
	switch( type )
	{
		case VALUE_INT:
			areasize = sizeof(int) *v->length_;
			break;
		case VALUE_DOUBLE:
			areasize = sizeof(double) *v->length_;
			break;
		case VALUE_STRING:
			areasize = sizeof(char) *v->granule_size_ *v->length_;
			break;
		default: assert( false ); break;
	}

	assert( areasize > 0 );
	v->data_ = xmalloc( areasize );
	memset( v->data_, 0, areasize );
	v->data_size_ = static_cast<int>( areasize );
}

list_t* create_variable_table()
{
	return create_list();
}

void destroy_variable_table( list_t* table )
{
	auto node =table->head_;
	while( node != nullptr )
	{
		const auto var =reinterpret_cast<variable_t*>( node->value_ );
		destroy_variable( var );
		node = node->next_;
	}

	list_free_all( *table );
	destroy_list( table );
}

variable_t* search_variable( list_t* table, const char* name )
{
	auto node =table->head_;
	while( node != nullptr )
	{
		const auto var =reinterpret_cast<variable_t*>( node->value_ );
		if ( string_equal_igcase( var->name_, name ) )
		{ return var; }
		node = node->next_;
	}
	return nullptr;
}

void variable_set( list_t* table, const value_t& v, const char* name, int idx )
{
	auto var =search_variable( table, name );
	if ( var == nullptr )
	{
		var = create_variable( name );
		auto node = create_list_node();
		node->value_ = var;
		list_append( *table, node );
	}

	variable_set( var, v, idx );
}

void variable_set( variable_t* var, const value_t& v, int idx )
{
	assert( var != nullptr );
	if ( var->type_ != v.type_ )
	{
		if ( idx > 0 )
		{
			raise_error( "型の異なる変数への代入@@ %s(%d)", var->name_, idx );
		}
		prepare_variable( var, v.type_, 64, 16 );
	}

	bool init_required = false;

	int granule_size = 0;
	if ( v.type_ == VALUE_STRING )
	{
		granule_size = static_cast<int>( strlen( v.svalue_ ) ) + 1;
	}
	if ( var->granule_size_ < granule_size )
	{
		init_required = true;
	}

	int len = var->length_;
	if ( idx < 0 )
	{
		raise_error( "負の添え字は無効です@@ %s(%d)", var->name_, idx );
	}
	if ( len <= idx )
	{
		raise_error( "存在しない添え字への代入@@ %s(%d)", var->name_, idx );
	}

	if ( init_required )
	{
		prepare_variable( var, v.type_, granule_size, len );
	}

	assert( var->type_ == v.type_ );
	auto data_ptr = variable_data_ptr( *var, idx );
	switch( var->type_ )
	{
		case VALUE_INT:
		{
			reinterpret_cast<int*>( data_ptr )[0] = v.ivalue_;
			break;
		}
		case VALUE_DOUBLE:
		{
			reinterpret_cast<double*>( data_ptr )[0] = v.dvalue_;
			break;
		}
		case VALUE_STRING:
		{
			strcpy( reinterpret_cast<char*>( data_ptr ), v.svalue_ );
			break;
		}
		default:
			assert( false );
			break;
	}
}

void variable_add( variable_t* var, const value_t& v, int idx )
{
	assert( var != nullptr );
	if ( var->type_ != v.type_ )
	{
		raise_error( "型の異なる変数への加算代入（+=）操作@@ %s(%d)", var->name_, idx );
		return;
	}

	assert( var->type_ == v.type_ );
	auto* data_ptr = variable_data_ptr( *var, idx );
	switch( var->type_ )
	{
	case VALUE_INT:
	{
		reinterpret_cast<int*>( data_ptr )[0] += v.ivalue_;
		break;
	}
	case VALUE_DOUBLE:
	{
		reinterpret_cast<double*>( data_ptr )[0] += v.dvalue_;
		break;
	}
	case VALUE_STRING:
	{
		auto* varstr = reinterpret_cast<char*>( data_ptr );
		const auto varstrlen = static_cast<int>( strlen( varstr ) );
		const auto vstrlen = static_cast<int>( strlen( v.svalue_ ) );
		// 再確保必要？
		if ( ( varstrlen + vstrlen ) > var->granule_size_ )
		{
			if ( idx > 0 )
			{
				raise_error( "文字列変数への加算代入操作：バッファオーバーフロー@@ %s(%d)", var->name_, idx );
				return;
			}
			// 再確保してコピー
			auto* pstr = create_string( varstr );
			prepare_variable( var, VALUE_STRING, varstrlen + vstrlen + 4, 1 );
			varstr = reinterpret_cast<char*>( data_ptr );
			strcpy( varstr, pstr );
			destroy_string( pstr );
		}
		strcpy( varstr + varstrlen, v.svalue_ );
		break;
	}
	default:
		assert( false );
		break;
	}
}

void variable_sub( variable_t* var, const value_t& v, int idx )
{
	assert( var != nullptr );
	if ( var->type_ != v.type_ )
	{
		raise_error( "型の異なる変数への減算代入（-=）操作@@ %s(%d)", var->name_, idx );
		return;
	}

	assert( var->type_ == v.type_ );
	auto* data_ptr = variable_data_ptr( *var, idx );
	switch( var->type_ )
	{
	case VALUE_INT:
	{
		reinterpret_cast<int*>( data_ptr )[0] -= v.ivalue_;
		break;
	}
	case VALUE_DOUBLE:
	{
		reinterpret_cast<double*>( data_ptr )[0] -= v.dvalue_;
		break;
	}
	case VALUE_STRING:
	{
		raise_error( "文字列に対する減算代入（-=）操作は定義されてません@@ %s(%d)", var->name_, idx );
		break;
	}
	default:
		assert( false );
		break;
	}
}

void variable_mul( variable_t* var, const value_t& v, int idx )
{
	assert( var != nullptr );
	if ( var->type_ != v.type_ )
	{
		raise_error( "型の異なる変数への乗算代入（*=）操作@@ %s(%d)", var->name_, idx );
		return;
	}

	assert( var->type_ == v.type_ );
	auto* data_ptr = variable_data_ptr( *var, idx );
	switch( var->type_ )
	{
	case VALUE_INT:
	{
		reinterpret_cast<int*>( data_ptr )[0] *= v.ivalue_;
		break;
	}
	case VALUE_DOUBLE:
	{
		reinterpret_cast<double*>( data_ptr )[0] *= v.dvalue_;
		break;
	}
	case VALUE_STRING:
	{
		raise_error( "文字列に対する乗算代入（*=）操作は定義されてません@@ %s(%d)", var->name_, idx );
		break;
	}
	default:
		assert( false );
		break;
	}
}

void variable_div( variable_t* var, const value_t& v, int idx )
{
	assert( var != nullptr );
	if ( var->type_ != v.type_ )
	{
		raise_error( "型の異なる変数への除算代入（/=）操作@@ %s(%d)", var->name_, idx );
		return;
	}

	assert( var->type_ == v.type_ );
	auto* data_ptr = variable_data_ptr( *var, idx );
	switch( var->type_ )
	{
	case VALUE_INT:
	{
		if ( v.ivalue_ == 0 )
		{
			raise_error( "0除算が行われました@@ %s(%d)", var->name_, idx );
			break;
		}
		reinterpret_cast<int*>( data_ptr )[0] /= v.ivalue_;
		break;
	}
	case VALUE_DOUBLE:
	{
		if ( v.dvalue_ == 0.0 )
		{
			raise_error( "0除算が行われました@@ %s(%d)", var->name_, idx );
			break;
		}
		reinterpret_cast<double*>( data_ptr )[0] /= v.dvalue_;
		break;
	}
	case VALUE_STRING:
	{
		raise_error( "文字列に対する除算代入（/=）操作は定義されてません@@ %s(%d)", var->name_, idx );
		break;
	}
	default:
		assert( false );
		break;
	}
}

void variable_mod( variable_t* var, const value_t& v, int idx )
{
	assert( var != nullptr );
	if ( var->type_ != v.type_ )
	{
		raise_error( "型の異なる変数への剰余代入（\\=）操作@@ %s(%d)", var->name_, idx );
		return;
	}

	assert( var->type_ == v.type_ );
	auto* data_ptr = variable_data_ptr( *var, idx );
	switch( var->type_ )
	{
	case VALUE_INT:
	{
		if ( v.ivalue_ == 0 )
		{
			raise_error( "0剰余が行われました@@ %s(%d)", var->name_, idx );
			break;
		}
		reinterpret_cast<int*>( data_ptr )[0] %= v.ivalue_;
		break;
	}
	case VALUE_DOUBLE:
	{
		if ( v.dvalue_ == 0.0 )
		{
			raise_error( "0.0剰余が行われました@@ %s(%d)", var->name_, idx );
			break;
		}
		auto* const vp = reinterpret_cast<double*>( data_ptr );
		vp[0] = std::fmod( vp[0], v.dvalue_ );
		break;
	}
	case VALUE_STRING:
	{
		raise_error( "文字列に対する剰余代入（\\=）操作は定義されてません@@ %s(%d)", var->name_, idx );
		break;
	}
	default:
		assert( false );
		break;
	}
}

void variable_bor( variable_t* var, const value_t& v, int idx )
{
	assert( var != nullptr );
	if ( var->type_ != v.type_ )
	{
		raise_error( "型の異なる変数へのOR代入（|=）操作@@ %s(%d)", var->name_, idx );
		return;
	}

	assert( var->type_ == v.type_ );
	auto* data_ptr = variable_data_ptr( *var, idx );
	switch( var->type_ )
	{
	case VALUE_INT:
	{
		reinterpret_cast<int*>( data_ptr )[0] |= v.ivalue_;
		break;
	}
	case VALUE_DOUBLE:
	{
		raise_error( "文字列に対するOR代入（|=）操作は定義されてません@@ %s(%d)", var->name_, idx );
		break;
	}
	case VALUE_STRING:
	{
		raise_error( "文字列に対するOR代入（|=）操作は定義されてません@@ %s(%d)", var->name_, idx );
		break;
	}
	default:
		assert( false );
		break;
	}
}

void variable_band( variable_t* var, const value_t& v, int idx )
{
	assert( var != nullptr );
	if ( var->type_ != v.type_ )
	{
		raise_error( "型の異なる変数へのAND代入（&=）操作@@ %s(%d)", var->name_, idx );
		return;
	}

	assert( var->type_ == v.type_ );
	auto* data_ptr = variable_data_ptr( *var, idx );
	switch( var->type_ )
	{
	case VALUE_INT:
	{
		reinterpret_cast<int*>( data_ptr )[0] &= v.ivalue_;
		break;
	}
	case VALUE_DOUBLE:
	{
		raise_error( "文字列に対するAND代入（&=）操作は定義されてません@@ %s(%d)", var->name_, idx );
		break;
	}
	case VALUE_STRING:
	{
		raise_error( "文字列に対するAND代入（&=）操作は定義されてません@@ %s(%d)", var->name_, idx );
		break;
	}
	default:
		assert( false );
		break;
	}
}

void variable_bxor( variable_t* var, const value_t& v, int idx )
{
	assert( var != nullptr );
	if ( var->type_ != v.type_ )
	{
		raise_error( "型の異なる変数へのXOR代入（^=）操作@@ %s(%d)", var->name_, idx );
		return;
	}

	assert( var->type_ == v.type_ );
	auto* data_ptr = variable_data_ptr( *var, idx );
	switch( var->type_ )
	{
	case VALUE_INT:
	{
		reinterpret_cast<int*>( data_ptr )[0] ^= v.ivalue_;
		break;
	}
	case VALUE_DOUBLE:
	{
		raise_error( "文字列に対するXOR代入（^=）操作は定義されてません@@ %s(%d)", var->name_, idx );
		break;
	}
	case VALUE_STRING:
	{
		raise_error( "文字列に対するXOR代入（|=）操作は定義されてません@@ %s(%d)", var->name_, idx );
		break;
	}
	default:
		assert( false );
		break;
	}
}

void* variable_data_ptr( const variable_t& v, int idx )
{
	if ( idx<0 || idx>=v.length_ )
	{
		raise_error( "変数への配列アクセスが範囲外です@@ %s(%d)", v.name_, idx );
	}

	switch( v.type_ )
	{
		case VALUE_INT:
		{
			return reinterpret_cast<int*>( v.data_ ) +idx;
		}
		case VALUE_DOUBLE:
		{
			return reinterpret_cast<double*>( v.data_ ) +idx;
		}
		case VALUE_STRING:
		{
			return reinterpret_cast<char*>( v.data_ ) + v.granule_size_ *idx;
		}
		default:
			assert( false );
			break;
	}
	return nullptr;
}

int variable_calc_int( const variable_t& r, int idx )
{
	const auto* const data_ptr = variable_data_ptr( r, idx );
	switch( r.type_ )
	{
		case VALUE_INT:		return *reinterpret_cast<const int*>( data_ptr );
		case VALUE_DOUBLE:	return static_cast<int>(*reinterpret_cast<const double*>( data_ptr ));
		case VALUE_STRING:	return atoi( reinterpret_cast<const char*>( data_ptr ) );
		default:
			assert( false );
			break;
	}
	return 0;
}

double variable_calc_double( const variable_t& r, int idx )
{
	const auto* const data_ptr = variable_data_ptr( r, idx );
	switch( r.type_ )
	{
		case VALUE_INT:		return static_cast<double>(*reinterpret_cast<const int*>( data_ptr ));
		case VALUE_DOUBLE:	return *reinterpret_cast<const double*>( data_ptr );
		case VALUE_STRING:	return atof( reinterpret_cast<const char*>( data_ptr ) );
		default:
			assert( false );
			break;
	}
	return 0.0;
}

const char* variable_get_string( const variable_t& r, int idx )
{
	if ( r.type_ != VALUE_STRING )
	{ return nullptr; }

	const auto* const data_ptr = variable_data_ptr( r, idx );
	return reinterpret_cast<const char*>( data_ptr );
}

char* variable_calc_string( const variable_t& r, int idx )
{
	const auto* const data_ptr = variable_data_ptr( r, idx );
	switch( r.type_ )
	{
		case VALUE_INT:		return create_string_from( *reinterpret_cast<const int*>( data_ptr ) );
		case VALUE_DOUBLE:	return create_string_from( *reinterpret_cast<const double*>( data_ptr ) );
		case VALUE_STRING:	return create_string( reinterpret_cast<const char*>( data_ptr ) );
		default:
			assert( false );
			break;
	}
	return create_string("");
}

//=============================================================================
// 値
value_t* create_value( int v )
{
	value_t* res =alloc_value();
	res->type_ = VALUE_INT;
	res->ivalue_ = v;
	return res;
}

value_t* create_value( double v )
{
	value_t* res =alloc_value();
	res->type_ = VALUE_DOUBLE;
	res->dvalue_ = v;
	return res;
}

value_t* create_value( const char* v )
{
	value_t* res =alloc_value();
	res->type_ = VALUE_STRING;
	res->svalue_ = create_string( v );
	return res;
}

value_t* create_value( variable_t* v, int idx )
{
	value_t* res =alloc_value();
	res->type_ = VALUE_VARIABLE;
	res->variable_ = v;
	res->index_ = idx;
	return res;
}

value_t* create_value( const value_t& v )
{
	value_t* res =alloc_value();
	res->type_ = v.type_;
	switch( v.type_ )
	{
		case VALUE_INT:			res->ivalue_ =v.ivalue_; break;
		case VALUE_DOUBLE:		res->dvalue_ =v.dvalue_; break;
		case VALUE_STRING:		res->svalue_ =create_string(v.svalue_); break;
		case VALUE_VARIABLE:	res->variable_ =v.variable_; res->index_ =v.index_; break;
		default: raise_error( "中身が入ってない値をコピーして作ろうとしました@@ ptr=%p", &v );
	}
	return res;
}

value_t* create_value_move( char* v )
{
	value_t* res =alloc_value();
	res->type_ = VALUE_STRING;
	res->svalue_ = v;
	return res;
}

void destroy_value( value_t* t )
{
	clear_value( t );
	free_value( t );
}

void value_set( value_t* v, int i )
{
	clear_value( v );
	v->type_ = VALUE_INT;
	v->ivalue_ = i;
}

void value_set( value_t* v, double d )
{
	clear_value( v );
	v->type_ = VALUE_DOUBLE;
	v->dvalue_ = d;
}

void value_set( value_t* v, const char* s )
{
	clear_value( v );
	v->type_ = VALUE_STRING;
	v->svalue_ = create_string( s );
}

void value_move( value_t* v, char* s )
{
	clear_value( v );
	v->type_ = VALUE_STRING;
	v->svalue_ = s;;
}

void value_move( value_t* to, value_t* from )
{
	clear_value( to );
	// ここも結構遅い、アーキテクチャ確定してれば早くできるが…
	memcpy( to, from, sizeof(*to) );
	memset( from, 0, sizeof(*from) );
	from->type_ = VALUE_NONE;
}

value_tag value_get_primitive_tag( const value_t& r )
{
	if ( r.type_ == VALUE_VARIABLE )
	{ return r.variable_->type_; }
	return r.type_;
}

bool value_calc_boolean( const value_t& r )
{
	switch( value_get_primitive_tag( r ) )
	{
		case VALUE_INT:			return value_calc_int(r) != 0;
		case VALUE_DOUBLE:		return value_calc_double(r) != 0.0;
		case VALUE_STRING:		return value_calc_int(r) != 0;
		default: assert( false ); break;
	}
	return false;
}

int value_calc_int( const value_t& r )
{
	switch( r.type_ )
	{
		case VALUE_INT:			return r.ivalue_;
		case VALUE_DOUBLE:		return static_cast<int>( r.dvalue_ );
		case VALUE_STRING:		return atoi( r.svalue_ );
		case VALUE_VARIABLE:	return variable_calc_int( *r.variable_, r.index_ ); break;
		default: assert( false ); break;
	}
	return 0;
}

double value_calc_double( const value_t& r )
{
	switch( r.type_ )
	{
		case VALUE_INT:			return static_cast<double>( r.ivalue_ );
		case VALUE_DOUBLE:		return r.dvalue_;
		case VALUE_STRING:		return atof( r.svalue_ );
		case VALUE_VARIABLE:	return variable_calc_double( *r.variable_, r.index_ ); break;
		default: assert( false ); break;
	}
	return 0.0;
}

const char* value_get_string( const value_t& r )
{
	const char* s =nullptr;
	switch( r.type_ )
	{
	case VALUE_STRING:		s = r.svalue_; break;
	case VALUE_VARIABLE:	s = variable_get_string( *r.variable_, r.index_ ); break;
	default: break;
	}
	return s;
}

char* value_calc_string( const value_t& r )
{
	char* s =nullptr;
	switch( r.type_ )
	{
		case VALUE_INT:			s = create_string_from( r.ivalue_ ); break;
		case VALUE_DOUBLE:		s = create_string_from( r.dvalue_ ); break;
		case VALUE_STRING:		s = create_string( r.svalue_ ); break;
		case VALUE_VARIABLE:	s = variable_calc_string( *r.variable_, r.index_ ); break;
		default: assert( false ); break;
	}
	return s;
}

value_t* value_convert_type( value_tag to, const value_t& r )
{
	if ( to == r.type_ )
	{ return create_value( r ); }

	switch( to )
	{
		case VALUE_INT:		return create_value( value_calc_int( r ) );
		case VALUE_DOUBLE:	return create_value( value_calc_double( r ) );
		case VALUE_STRING:
		{
			auto s = value_calc_string( r );
			auto res = create_value_move( s );
			return res;
		}
		default: assert( false ); break;
	}
	return nullptr;
}

void value_isolate( value_t& v )
{
	if ( v.type_ != VALUE_VARIABLE )
	{ return; }

	switch( v.variable_->type_ )
	{
		case VALUE_INT:
			value_set( &v, variable_calc_int( *v.variable_, v.index_ ) );
			break;
		case VALUE_DOUBLE:
			value_set( &v, variable_calc_double( *v.variable_, v.index_ ) );
			break;
		case VALUE_STRING:
			value_move( &v, variable_calc_string( *v.variable_, v.index_ ) );
			break;
		default:
			assert( false );
			break;
	}
}

void value_bor( value_t* v, const value_t& r )
{
	value_t* t = value_convert_type( v->type_, r );
	switch( v->type_ )
	{
		case VALUE_INT:		value_set( v, v->ivalue_ | t->ivalue_ ); break;
		case VALUE_DOUBLE:
		{
			raise_error( "浮動小数点同士の|演算子は挙動が定義されていません" );
			break;
		}
		case VALUE_STRING:
		{
			raise_error( "文字列同士の|演算子は挙動が定義されていません" );
			break;
		}
		default: assert( false ); break;
	}
	destroy_value( t );
}

void value_band( value_t* v, const value_t& r )
{
	value_t* t = value_convert_type( v->type_, r );
	switch( v->type_ )
	{
		case VALUE_INT:		value_set( v, v->ivalue_ & t->ivalue_ ); break;
		case VALUE_DOUBLE:
		{
			raise_error( "浮動小数点同士の&演算子は挙動が定義されていません" );
			break;
		}
		case VALUE_STRING:
		{
			raise_error( "文字列同士の&演算子は挙動が定義されていません" );
			break;
		}
		default: assert( false ); break;
	}
	destroy_value( t );
}

void value_bxor( value_t* v, const value_t& r )
{
	value_t* t = value_convert_type( v->type_, r );
	switch( v->type_ )
	{
	case VALUE_INT:		value_set( v, v->ivalue_ ^ t->ivalue_ ); break;
	case VALUE_DOUBLE:
	{
		raise_error( "浮動小数点同士の^演算子は挙動が定義されていません" );
		break;
	}
	case VALUE_STRING:
	{
		raise_error( "文字列同士の^演算子は挙動が定義されていません" );
		break;
	}
	default: assert( false ); break;
	}
	destroy_value( t );
}

void value_eq( value_t* v, const value_t& r )
{
	value_t* t = value_convert_type( v->type_, r );
	switch( v->type_ )
	{
		case VALUE_INT:		value_set( v, v->ivalue_==t->ivalue_ ? 1 : 0 ); break;
		case VALUE_DOUBLE:	value_set( v, v->dvalue_==t->dvalue_ ? 1 : 0 ); break;
		case VALUE_STRING:	value_set( v, strcmp(v->svalue_, t->svalue_)==0 ? 1: 0 ); break;
		default: assert( false ); break;
	}
	destroy_value( t );
}

void value_neq( value_t* v, const value_t& r )
{
	value_t* t = value_convert_type( v->type_, r );
	switch( v->type_ )
	{
		case VALUE_INT:		value_set( v, v->ivalue_!=t->ivalue_ ? 1 : 0 ); break;
		case VALUE_DOUBLE:	value_set( v, v->dvalue_!=t->dvalue_ ? 1 : 0 ); break;
		case VALUE_STRING:	value_set( v, strcmp(v->svalue_, t->svalue_)!=0 ? 1: 0 ); break;
		default: assert( false ); break;
	}
	destroy_value( t );
}

void value_gt( value_t* v, const value_t& r )
{
	value_t* t = value_convert_type( v->type_, r );
	switch( v->type_ )
	{
		case VALUE_INT:		value_set( v, v->ivalue_>t->ivalue_ ? 1 : 0 ); break;
		case VALUE_DOUBLE:	value_set( v, v->dvalue_>t->dvalue_ ? 1 : 0 ); break;
		case VALUE_STRING:
		{
			raise_error( "文字列同士の>演算子は挙動が定義されていません" );
			break;
		}
		default: assert( false ); break;
	}
	destroy_value( t );
}

void value_gtoe( value_t* v, const value_t& r )
{
	value_t* t = value_convert_type( v->type_, r );
	switch( v->type_ )
	{
		case VALUE_INT:		value_set( v, v->ivalue_>=t->ivalue_ ? 1 : 0 ); break;
		case VALUE_DOUBLE:	value_set( v, v->dvalue_>=t->dvalue_ ? 1 : 0 ); break;
		case VALUE_STRING:
		{
			raise_error( "文字列同士の>=演算子は挙動が定義されていません" );
			break;
		}
		default: assert( false ); break;
	}
	destroy_value( t );
}

void value_lt( value_t* v, const value_t& r )
{
	value_t* t = value_convert_type( v->type_, r );
	switch( v->type_ )
	{
		case VALUE_INT:		value_set( v, v->ivalue_<t->ivalue_ ? 1 : 0 ); break;
		case VALUE_DOUBLE:	value_set( v, v->dvalue_<t->dvalue_ ? 1 : 0 ); break;
		case VALUE_STRING:
		{
			raise_error( "文字列同士の<演算子は挙動が定義されていません" );
			break;
		}
		default: assert( false ); break;
	}
	destroy_value( t );
}

void value_ltoe( value_t* v, const value_t& r )
{
	value_t* t = value_convert_type( v->type_, r );
	switch( v->type_ )
	{
		case VALUE_INT:		value_set( v, v->ivalue_<=t->ivalue_ ? 1 : 0 ); break;
		case VALUE_DOUBLE:	value_set( v, v->dvalue_<=t->dvalue_ ? 1 : 0 ); break;
		case VALUE_STRING:
		{
			raise_error( "文字列同士の<=演算子は挙動が定義されていません" );
			break;
		}
		default: assert( false ); break;
	}
	destroy_value( t );
}

void value_add( value_t* v, const value_t& r )
{
	value_t* t = value_convert_type( v->type_, r );
	switch( v->type_ )
	{
		case VALUE_INT:		v->ivalue_ += t->ivalue_; break;
		case VALUE_DOUBLE:	v->dvalue_ += t->dvalue_; break;
		case VALUE_STRING:
		{
			const auto required = snprintf( nullptr, 0, "%s%s", v->svalue_, t->svalue_ );
			auto s = create_string( required );
			sprintf( s, "%s%s", v->svalue_, t->svalue_ );
			destroy_string( v->svalue_ );
			v->svalue_ = s;
			break;
		}
		default: assert( false ); break;
	}
	destroy_value( t );
}

void value_sub( value_t* v, const value_t& r )
{
	value_t* t = value_convert_type( v->type_, r );
	switch( v->type_ )
	{
		case VALUE_INT:		v->ivalue_ -= t->ivalue_; break;
		case VALUE_DOUBLE:	v->dvalue_ -= t->dvalue_; break;
		case VALUE_STRING:
		{
			raise_error( "文字列同士の-演算子は挙動が定義されていません" );
			break;
		}
		default: assert( false ); break;
	}
	destroy_value( t );
}

void value_mul( value_t* v, const value_t& r )
{
	value_t* t = value_convert_type( v->type_, r );
	switch( v->type_ )
	{
		case VALUE_INT:		v->ivalue_ *= t->ivalue_; break;
		case VALUE_DOUBLE:	v->dvalue_ *= t->dvalue_; break;
		case VALUE_STRING:
		{
			raise_error( "文字列同士の*演算子は挙動が定義されていません" );
			break;
		}
		default: assert( false ); break;
	}
	destroy_value( t );
}

void value_div( value_t* v, const value_t& r )
{
	value_t* t = value_convert_type( v->type_, r );
	switch( v->type_ )
	{
		case VALUE_INT:
			if ( t->ivalue_ == 0 )
			{
				raise_error( "0除算が行われました" );
				break;
			}
			v->ivalue_ /= t->ivalue_;
			break;
		case VALUE_DOUBLE:
			if ( t->dvalue_ == 0.0 )
			{
				raise_error( "0.0除算が行われました" );
				break;
			}
			v->dvalue_ /= t->dvalue_;
			break;
		case VALUE_STRING:
		{
			raise_error( "文字列同士の/演算子は挙動が定義されていません" );
			break;
		}
		default: assert( false ); break;
	}
	destroy_value( t );
}

void value_mod( value_t* v, const value_t& r )
{
	value_t* t = value_convert_type( v->type_, r );
	switch( v->type_ )
	{
		case VALUE_INT:
			if ( t->ivalue_ == 0 )
			{
				raise_error( "0剰余が行われました" );
				break;
			}
			v->ivalue_ %= t->ivalue_;
			break;
		case VALUE_DOUBLE:
			if ( t->dvalue_ == 0.0 )
			{
				raise_error( "0.0除算が行われました" );
				break;
			}
			v->dvalue_ = std::fmod(v->dvalue_, t->dvalue_);
			break;
		case VALUE_STRING:
		{
			raise_error( "文字列同士の\\演算子は挙動が定義されていません" );
			break;
		}
		default: assert( false ); break;
	}
	destroy_value( t );
}

void value_unary_minus( value_t* v )
{
	switch( v->type_ )
	{
		case VALUE_INT:		v->ivalue_ = -v->ivalue_; break;
		case VALUE_DOUBLE:	v->dvalue_ = -v->dvalue_; break;
		case VALUE_STRING:	raise_error( "文字列に負値は存在しません[%s]", v->svalue_ ); break;
		default: assert( false ); break;
	}
}

//=============================================================================
// スタック
value_stack_t* create_value_stack()
{
	auto res = reinterpret_cast<value_stack_t*>( xmalloc( sizeof(value_stack_t) ) );
	initialize_value_stack( res );
	return res;
}

void destroy_value_stack( value_stack_t* st )
{
	uninitialize_value_stack( st );
	xfree( st );
}

void initialize_value_stack( value_stack_t* st )
{
	const auto l =16;// 初期サイズ
	st->stack_ = reinterpret_cast<value_t**>( xmalloc( sizeof(value_t*) *l ) );
	st->top_ = 0;
	st->max_ = l;
}

void uninitialize_value_stack( value_stack_t* st )
{
	stack_pop( st, st->top_ );
	xfree( st->stack_ );
	st->stack_ = nullptr;
	st->top_ = 0;
	st->max_ = 0;
}

void stack_push( value_stack_t* st, value_t* v )
{
	if ( st->top_+1 > st->max_ )
	{
		st->max_ = st->max_ *2;// 貪欲
		st->stack_ = reinterpret_cast<value_t**>( xrealloc( st->stack_, sizeof(value_t*) *st->max_ ) );
	}

	st->stack_[st->top_] =v;
	++st->top_;
}

void stack_push( value_stack_t* st, const value_t& v )
{
	value_t* vp = create_value( v );
	stack_push( st, vp );
}

value_t* stack_peek( value_stack_t* st, int i )
{
	const auto idx = ( i<0 ? st->top_ +i : i );
	assert( idx>=0 && idx<st->top_ );
	return st->stack_[ idx ];
}

void stack_pop( value_stack_t* st, size_t n )
{
	assert( n <= static_cast<size_t>(st->top_) );
	while( n-- > 0 )
	{
		--st->top_;
		assert( st->stack_[ st->top_ ] != nullptr );
		destroy_value( st->stack_[ st->top_ ] );
		st->stack_[ st->top_ ] = nullptr;
	}
}

//=============================================================================
// システム変数
int query_sysvar( const char* s )
{
	static const struct
	{
		int				tag_;
		const char*		word_;
	} table[] =
	{
		{ SYSVAR_CNT,		"cnt", },
		{ SYSVAR_STAT,		"stat", },
		{ SYSVAR_REFDVAL,	"refdval", },
		{ SYSVAR_REFSTR,	"refstr", },
		{ SYSVAR_STRSIZE,	"strsize", },
		{ SYSVAR_LOOPLEV,	"looplev", },
		{ -1,				nullptr },
	};

	// 全探索
	for( int i=0; table[i].tag_!=-1; ++i )
	{
		if ( string_equal_igcase( s, table[i].word_ ) )
		{ return table[i].tag_; }
	}
	return -1;
}

//=============================================================================
// 実行コード
code_container_t* create_code_container()
{
	auto res = reinterpret_cast<code_container_t*>( xmalloc( sizeof(code_container_t) ) );
	res->code_ = nullptr;
	res->code_size_ = 0;
	res->code_buffer_size_ = 0;
	return res;
}

void destroy_code_container( code_container_t* c )
{
	if ( c->code_ != nullptr )
	{ xfree( c->code_ ); }
	xfree( c );
}

//=============================================================================
// 実行環境
execute_environment_t* create_execute_environment()
{
	auto res = reinterpret_cast<execute_environment_t*>( xmalloc( sizeof( execute_environment_t ) ) );
	res->parser_list_ = create_list();
	res->ast_list_ = create_list();
	res->label_table_ = create_list();
	res->variable_table_ = create_variable_table();
	res->execute_code_ = create_code_container();
	return res;
}

void destroy_execute_environment( execute_environment_t* e )
{
	{
		auto node =e->parser_list_->head_;
		while( node != nullptr )
		{
			const auto parser = reinterpret_cast<parse_context_t*>( node->value_ );
			uninitialize_parse_context( parser );
			destroy_parse_context( parser );
			node = node->next_;
		}
		list_free_all( *e->parser_list_ );
		destroy_list( e->parser_list_ );
	}
	{
		auto node =e->ast_list_->head_;
		while( node != nullptr )
		{
			const auto ast = reinterpret_cast<list_t*>( node->value_ );
			destroy_ast( ast );
			node = node->next_;
		}
		list_free_all( *e->ast_list_ );
		destroy_list( e->ast_list_ );
	}
	{
		auto node = e->label_table_->head_;
		while( node != nullptr )
		{
			const auto label_node =reinterpret_cast<label_node_t*>( node->value_ );
			xfree( label_node->name_ );
			xfree( label_node );
			node->value_ = nullptr;
			node = node->next_;
		}
		list_free_all( *e->label_table_ );
		destroy_list( e->label_table_ );
	}
	{
		destroy_code_container( e->execute_code_ );
	}
	destroy_variable_table( e->variable_table_ );
	xfree( e );
}

void initialize_execute_status( execute_status_t* s )
{
	s->stack_ = create_value_stack();
	s->pc_ = 0;
	s->current_call_frame_ = 0;
	s->call_frame_[0].caller_poisition_ = 0;
	s->current_loop_frame_ = 0;
	s->loop_frame_[0].start_position_ = 0;
	s->loop_frame_[0].counter_ = 0;
	s->loop_frame_[0].max_ = 0;
	s->loop_frame_[0].cnt_ = 0;
	s->is_end_ = false;
	s->stat_ =0;
	s->refdval_ = 0.0;
	s->refstr_ = create_string( "" );
	s->strsize_ = 0;
}

void uninitialize_execute_status( execute_status_t* s )
{
	destroy_value_stack( s->stack_ );
	destroy_string( s->refstr_ );
	s->refstr_ = nullptr;
}

void load_script( execute_environment_t* e, const char* script, const load_arg_t* arg )
{
	// プリプロセス
	char* preprocessed = prepro_do( script );

	if ( arg && arg->dump_preprocessed_ )
	{
		printf( "====PREPROCESSED SCRIPT FILE(%d bytes)\n----begin----\n%s\n----end----\n", static_cast<int>( strlen( preprocessed ) ), preprocessed );
	}

	// パース
	tokenize_context_t tokenizer;
	initialize_tokenize_context( &tokenizer, preprocessed );

	auto parser = create_parse_context();
	initialize_parse_context( parser, tokenizer );

	auto ast = parse_script( *parser );

	if ( arg && arg->dump_ast_ )
	{
		dump_ast( ast );
	}

	uninitialize_tokenize_context( &tokenizer );

	destroy_string( preprocessed );
	preprocessed = nullptr;

	// 特定の部分木マッチング
	// 先に必要な変数のインスタンスを作っておき、ラベルテーブルも生成しておく
	{
		struct _
		{
			static void walk( execute_environment_t* e, ast_node_t* node )
			{
				if ( node->tag_==NODE_VARIABLE || node->tag_==NODE_IDENTIFIER_EXPR/*変数配列の可能性あり*/ )
				{
					auto var_name = node->token_->content_;
					if ( search_variable( e->variable_table_, var_name ) == nullptr )
					{
						// 適当な変数として初期化しておく
						value_t v;
						v.type_ = VALUE_INT;
						v.ivalue_ = 0;
						variable_set( e->variable_table_, v, var_name, 0 );
					}
				}
				else if ( node->tag_ == NODE_LABEL )
				{
					auto label_node = create_list_node();
					label_node_t* label =reinterpret_cast<label_node_t*>( xmalloc( sizeof(label_node_t) ) );
					label->name_ = create_string( node->token_->content_ );
					label_node->value_ = label;
					list_append( *e->label_table_, label_node );
				}
				if ( node->left_ != nullptr )
				{ walk( e, node->left_ ); }
				if ( node->right_ != nullptr )
				{ walk( e, node->right_ ); }
			}
		};

		list_node_t* st =ast->head_;
		while( st != nullptr )
		{
			ast_node_t* node = reinterpret_cast<ast_node_t*>( st->value_ );
			_::walk( e, node );
			st = st->next_;
		}
	}

	// コード生成
	generate_and_append_code( e, ast );

	// パーサーとASTを保存しておく
	{
		auto parser_node = create_list_node();
		parser_node->value_ = parser;
		list_append( *e->parser_list_, parser_node );
	}
	{
		auto ast_node = create_list_node();
		ast_node->value_ = ast;
		list_append( *e->ast_list_, ast_node );
	}
}

void execute_inner( execute_environment_t* e, execute_status_t* s )
{
	const code_t* codes =e->execute_code_->code_;
	const auto code_size = static_cast<int>(e->execute_code_->code_size_);

	auto& pc = s->pc_;

	for( ; ; )
	{

		// もう実行おわってる
		if ( s->is_end_ )
		{ break; }

		// 末尾に到達してる
		if ( code_size <= pc )
		{ break; }

		const auto op = codes[ pc ];
		switch( op )
		{
			case OPERATOR_NOP:
				break;

			case OPERATOR_PUSH_INT:
				stack_push( s->stack_, create_value( codes[ pc +1 ] ) );
				++pc;
				break;

			case OPERATOR_PUSH_DOUBLE:
			{
				double v =0.0;
				const auto stride = code_get_block( v, codes, pc +1 );
				stack_push( s->stack_, create_value( v ) );
				pc += stride;
				break;
			}

			case OPERATOR_PUSH_STRING:
			{
				const char* v =nullptr;
				const auto stride = code_get_block( v, codes, pc +1 );
				stack_push( s->stack_, create_value( v ) );
				pc += stride;
				break;
			}

			case OPERATOR_PUSH_VARIABLE:
			{
				variable_t* var =nullptr;
				const auto stride = code_get_block( var, codes, pc +1 );

				assert( s->stack_->top_ >= 1 );
				const auto i = stack_peek( s->stack_ );
				const auto idx = value_calc_int( *i );
				stack_pop( s->stack_, 1 );
				stack_push( s->stack_, create_value( var, idx ) );

				pc += stride;
				break;
			}

			case OPERATOR_PUSH_SYSVAR:
			{
				const auto sysvar = codes[ pc +1 ];
				switch( sysvar )
				{
					case SYSVAR_CNT:
						if ( s->current_loop_frame_ <= 0 )
						{
							raise_error( "システム変数cnt：repeat-loop中でないのに参照しました" );
						}
						stack_push( s->stack_, create_value( s->loop_frame_[s->current_loop_frame_ -1].cnt_ ) );
						break;
					case SYSVAR_STAT:
						stack_push( s->stack_, create_value( s->stat_ ) );
						break;
					case SYSVAR_REFDVAL:
						stack_push( s->stack_, create_value( s->refdval_ ) );
						break;
					case SYSVAR_REFSTR:
						stack_push( s->stack_, create_value( s->refstr_ ) );
						break;
					case SYSVAR_STRSIZE:
						stack_push( s->stack_, create_value( s->strsize_ ) );
						break;
					case SYSVAR_LOOPLEV:
						stack_push( s->stack_, create_value( s->current_loop_frame_ ) );
						break;
					default:
						assert( false );
						stack_push( s->stack_, create_value( 0 ) );
						break;
				}
				++pc;
				break;
			}

			case OPERATOR_ASSIGN:
			case OPERATOR_ADD_ASSIGN:
			case OPERATOR_SUB_ASSIGN:
			case OPERATOR_MUL_ASSIGN:
			case OPERATOR_DIV_ASSIGN:
			case OPERATOR_MOD_ASSIGN:
			case OPERATOR_BOR_ASSIGN:
			case OPERATOR_BAND_ASSIGN:
			case OPERATOR_BXOR_ASSIGN:
			{
				assert( s->stack_->top_ >= 2 );

				const auto var =stack_peek( s->stack_, -2 );
				if ( var->type_ != VALUE_VARIABLE )
				{
					raise_error( "変数代入：代入先が変数ではありませんでした" );
				}

				const auto v =stack_peek( s->stack_, -1 );
				value_isolate( *v );
				auto* t = ( op == OPERATOR_ASSIGN ? nullptr : value_convert_type( value_get_primitive_tag( *var ), *v ) );
				switch( op )
				{
				case OPERATOR_ASSIGN:		variable_set( var->variable_, *v, var->index_ ); break;
				case OPERATOR_ADD_ASSIGN:	variable_add( var->variable_, *t, var->index_ ); break;
				case OPERATOR_SUB_ASSIGN:	variable_sub( var->variable_, *t, var->index_ ); break;
				case OPERATOR_MUL_ASSIGN:	variable_mul( var->variable_, *t, var->index_ ); break;
				case OPERATOR_DIV_ASSIGN:	variable_div( var->variable_, *t, var->index_ ); break;
				case OPERATOR_MOD_ASSIGN:	variable_mod( var->variable_, *t, var->index_ ); break;
				case OPERATOR_BOR_ASSIGN:	variable_bor( var->variable_, *t, var->index_ ); break;
				case OPERATOR_BAND_ASSIGN:	variable_band( var->variable_, *t, var->index_ ); break;
				case OPERATOR_BXOR_ASSIGN:	variable_bxor( var->variable_, *t, var->index_ ); break;
				default: assert( false ); break;
				}
				if ( t != nullptr )
				{
					destroy_value( t );
					t = nullptr;
				}
				stack_pop( s->stack_, 2 );
				break;
			}

			case OPERATOR_BOR:
			case OPERATOR_BAND:
			case OPERATOR_BXOR:
			case OPERATOR_EQ:
			case OPERATOR_NEQ:
			case OPERATOR_GT:
			case OPERATOR_GTOE:
			case OPERATOR_LT:
			case OPERATOR_LTOE:
			case OPERATOR_ADD:
			case OPERATOR_SUB:
			case OPERATOR_MUL:
			case OPERATOR_DIV:
			case OPERATOR_MOD:
			{
				assert( s->stack_->top_ >= 2 );
				value_t* l =stack_peek( s->stack_, -2 );
				value_t* r =stack_peek( s->stack_, -1 );
				value_isolate( *l );

				switch( op )
				{
					case OPERATOR_BOR:		value_bor( l, *r ); break;
					case OPERATOR_BAND:		value_band( l, *r ); break;
					case OPERATOR_BXOR:		value_bxor( l, *r ); break;
					case OPERATOR_EQ:		value_eq( l, *r ); break;
					case OPERATOR_NEQ:		value_neq( l, *r ); break;
					case OPERATOR_GT:		value_gt( l, *r ); break;
					case OPERATOR_GTOE:		value_gtoe( l, *r ); break;
					case OPERATOR_LT:		value_lt( l, *r ); break;
					case OPERATOR_LTOE:		value_ltoe( l, *r ); break;
					case OPERATOR_ADD:		value_add( l, *r ); break;
					case OPERATOR_SUB:		value_sub( l, *r ); break;
					case OPERATOR_MUL:		value_mul( l, *r ); break;
					case OPERATOR_DIV:		value_div( l, *r ); break;
					case OPERATOR_MOD:		value_mod( l, *r ); break;
					default: assert( false ); break;
				}
				stack_pop( s->stack_ );
				break;
			}

			case OPERATOR_UNARY_MINUS:
			{
				assert( s->stack_->top_ >= 1 );
				value_t* v =stack_peek( s->stack_ );
				value_isolate( *v );
				value_unary_minus( v );
				break;
			}

			case OPERATOR_IF:
			{
				const auto cond = stack_peek( s->stack_ );
				const auto is_cond = value_calc_boolean( *cond );
				stack_pop( s->stack_ );
				if ( is_cond )
				{
					++pc;
				}
				else
				{
					const auto false_head = codes[ pc +1 ];
					pc += false_head -1;
				}
				break;
			}

			case OPERATOR_REPEAT:
			{
				if ( s->current_loop_frame_ +1 >= MAX_LOOP_FRAME )
				{
					raise_error( "repeat：ネストが深すぎます" );
				}

				const auto end_position = codes[ pc +1 ];

				auto& frame = s->loop_frame_[s->current_loop_frame_];
				++s->current_loop_frame_;
				frame.start_position_ = pc +2;
				frame.end_position_ = end_position;
				frame.cnt_ = 0;
				frame.counter_ = 0;
				frame.max_ = 0;

				int loop_num =-1;
				const auto v = stack_peek( s->stack_ );
				loop_num = value_calc_int( *v );
				stack_pop( s->stack_ );

				frame.max_ = loop_num;
				++pc;
				break;
			}
			case OPERATOR_REPEAT_CHECK:
			{
				assert( s->current_loop_frame_ > 0 );
				auto& frame = s->loop_frame_[ s->current_loop_frame_ -1 ];
				if ( frame.max_>=0 && frame.counter_>=frame.max_ )
				{
					pc = frame.end_position_;
					--s->current_loop_frame_;
				}
				break;
			}

			case OPERATOR_LOOP:
			case OPERATOR_CONTINUE:
			{
				if ( s->current_loop_frame_ <= 0 )
				{
					raise_error( "loop,continue：repeat-loopの中にありません" );
				}

				auto& frame = s->loop_frame_[ s->current_loop_frame_ -1 ];
				++frame.counter_;
				++frame.cnt_;
				pc = frame.start_position_ -1;
				break;
			}
			case OPERATOR_BREAK:
			{
				if ( s->current_loop_frame_ <= 0 )
				{
					raise_error( "break：repeat-loopの中にありません" );
				}

				auto& frame = s->loop_frame_[ s->current_loop_frame_ -1 ];
				pc = frame.end_position_;

				--s->current_loop_frame_;
				break;
			}

			case OPERATOR_LABEL:
				break;

			case OPERATOR_GOSUB:
			{
				if ( (s->current_call_frame_ +1) >= MAX_CALL_FRAME )
				{
					raise_error( "gosub：ネストが深すぎます" );
				}

				label_node_t* label =nullptr;
				const auto stride = code_get_block( label, codes, pc +1 );
				assert( label != nullptr );

				auto& frame = s->call_frame_[s->current_call_frame_];
				++s->current_call_frame_;
				frame.caller_poisition_ = pc +stride;

				pc = label->position_ -1;
				break;
			}
			case OPERATOR_GOTO:
			{
				label_node_t* label =nullptr;
				code_get_block( label, codes, pc +1 );
				assert( label != nullptr );
				pc = label->position_ -1;
				break;
			}

			case OPERATOR_COMMAND:
			{
				const auto command = codes[ pc +1 ];
				assert( command >= 0 );
				const auto arg_num = codes[ pc +2 ];

					// コマンド呼び出し
				const auto delegate = get_command_delegate( static_cast<builtin_command_tag>( command ) );
				assert( delegate != nullptr );
				const auto top = s->stack_->top_;
				delegate( e, s, arg_num );
				assert( s->stack_->top_ == top -arg_num );// 戻り値がないことを確認

				pc += 2;
				break;
			}
			case OPERATOR_FUNCTION:
			{
				const auto function = codes[ pc +1 ];
				assert( function >= 0 );
				const auto arg_num = codes[ pc +2 ];

					// 関数呼び出し
				const auto delegate = get_function_delegate( static_cast<builtin_function_tag>( function ) );
				assert( delegate != nullptr );
				const auto top = s->stack_->top_;
				delegate( e, s, arg_num );
				assert( s->stack_->top_ == top -arg_num +1 );// 戻り値が入っていることを確認する

				 pc += 2;
				break;
			}

			case OPERATOR_JUMP:
			{
				pc = codes[ pc +1 ] -1;
				break;
			}
			case OPERATOR_JUMP_RELATIVE:
			{
				pc += codes[ pc +1 ] -1;
				break;
			}
			case OPERATOR_RETURN:
			{
				if ( s->current_call_frame_ <= 0 )
				{
					raise_error( "サブルーチン外からのreturnは無効です" );
				}

				const auto arg_num = codes[ pc +1 ];
				if ( arg_num > 0 )
				{
					assert( arg_num == 1 );
					const auto res = stack_peek( s->stack_ );
					switch( value_get_primitive_tag(*res) )
					{
						case VALUE_INT:		s->stat_ = value_calc_int(*res); break;
						case VALUE_DOUBLE:	s->refdval_ = value_calc_double(*res); break;
						case VALUE_STRING:	destroy_string( s->refstr_ ); s->refstr_ = value_calc_string(*res); break;
						default: assert( false ); break;
					}
					stack_pop( s->stack_ );
				}

				--s->current_call_frame_;
				const auto& frame =s->call_frame_[ s->current_call_frame_ ];
				pc = frame.caller_poisition_;
				break;
			}

			case OPERATOR_END:
				s->is_end_ =true;
				break;

			default: assert( false ); break;
		}

		++pc;
	}
}

void execute( execute_environment_t* e, int initial_pc )
{
	execute_status_t s;
	initialize_execute_status( &s );
	s.pc_ = initial_pc;

	if ( e->execute_code_->code_ == nullptr )
	{
		raise_error( "実行できるノードがありません@@ [%p]", e );
	}

	execute_inner( e, &s );

	uninitialize_execute_status( &s );
}

void generate_and_append_code( execute_environment_t* e, list_t* ast )
{
	struct generate_context_t
	{
		int			stack_;

		int			repeat_head_[32];
		int			repeat_depth_;
	};

	generate_context_t context;
	context.stack_ = 0;
	context.repeat_depth_ = 0;

	list_node_t* st =ast->head_;
	while( st != nullptr )
	{
		ast_node_t* node = reinterpret_cast<ast_node_t*>( st->value_ );

		struct _
		{
			static void walk( execute_environment_t* e, const ast_node_t* n, generate_context_t* c )
			{
				switch( n->tag_ )
				{
					case NODE_EMPTY:
						break;

					case NODE_LABEL:
					{
						const auto label_name = n->token_->content_;
						const auto label = search_label( e, label_name );
						assert( label != nullptr );
						label->position_ = static_cast<int>( e->execute_code_->code_size_ );

						code_write( e, OPERATOR_LABEL );
						break;
					}

					case NODE_BLOCK_STATEMENTS:
						if ( n->left_ )
						{ walk( e, n->left_, c ); }
						if ( n->right_ )
						{ walk( e, n->right_, c ); }
						break;

					case NODE_COMMAND:
					{
						assert( n->token_->tag_ == TOKEN_IDENTIFIER );
						const auto command_name = n->token_->content_;

						const auto command = query_command( command_name );
						if ( command == -1 )
						{
							raise_error( "コマンドが見つかりません：%s", command_name );
						}

						const auto top = c->stack_;
						if ( n->left_ != nullptr )
						{
							walk( e, n->left_, c );
						}
						const auto arg_num = c->stack_ -top;

						code_write( e, OPERATOR_COMMAND );
						code_write( e, command );
						code_write( e, arg_num );

						c->stack_ = top;
						break;
					}
					case NODE_ARGUMENTS:
					{
						if ( n->left_ != nullptr )
						{ walk( e, n->left_, c ); }
						if ( n->right_ != nullptr )
						{ walk( e, n->right_, c ); }
						break;
					}

					case NODE_ASSIGN:
					case NODE_ADD_ASSIGN:
					case NODE_SUB_ASSIGN:
					case NODE_MUL_ASSIGN:
					case NODE_DIV_ASSIGN:
					case NODE_MOD_ASSIGN:
					case NODE_BOR_ASSIGN:
					case NODE_BAND_ASSIGN:
					case NODE_BXOR_ASSIGN:
					{
						walk( e, n->left_, c );
						walk( e, n->right_, c );
						switch( n->tag_ )
						{
						case NODE_ASSIGN:		code_write( e, OPERATOR_ASSIGN ); break;
						case NODE_ADD_ASSIGN:	code_write( e, OPERATOR_ADD_ASSIGN ); break;
						case NODE_SUB_ASSIGN:	code_write( e, OPERATOR_SUB_ASSIGN ); break;
						case NODE_MUL_ASSIGN:	code_write( e, OPERATOR_MUL_ASSIGN ); break;
						case NODE_DIV_ASSIGN:	code_write( e, OPERATOR_DIV_ASSIGN ); break;
						case NODE_MOD_ASSIGN:	code_write( e, OPERATOR_MOD_ASSIGN ); break;
						case NODE_BOR_ASSIGN:	code_write( e, OPERATOR_BOR_ASSIGN ); break;
						case NODE_BAND_ASSIGN:	code_write( e, OPERATOR_BAND_ASSIGN ); break;
						case NODE_BXOR_ASSIGN:	code_write( e, OPERATOR_BXOR_ASSIGN ); break;
						default: assert( false ); break;
						}
						c->stack_ -= 2; 
						break;
					}
					case NODE_VARIABLE:
					{
						auto idx_node = n->left_;
						if ( idx_node )
						{
							walk( e, idx_node, c );
						}
						else
						{
							code_write( e, OPERATOR_PUSH_INT );
							code_write( e, 0 );
						}

						const auto var_name = n->token_->content_;
						const auto var = search_variable( e->variable_table_, var_name );
						assert( var != nullptr );

						code_write( e, OPERATOR_PUSH_VARIABLE );
						code_write( e, var );
						++c->stack_;
						break;
					}

					case NODE_EXPRESSION:
						assert( n->left_ != nullptr );
						walk( e, n->left_, c );
						break;

					case NODE_BOR:
					case NODE_BAND:
					case NODE_BXOR:
					case NODE_EQ:
					case NODE_NEQ:
					case NODE_GT:
					case NODE_GTOE:
					case NODE_LT:
					case NODE_LTOE:
					case NODE_ADD:
					case NODE_SUB:
					case NODE_MUL:
					case NODE_DIV:
					case NODE_MOD:
					{
						assert( n->left_ != nullptr );
						walk( e, n->left_, c );
						assert( n->right_ != nullptr );
						walk( e, n->right_, c );

						switch( n->tag_ )
						{
							case NODE_BOR:		code_write( e, OPERATOR_BOR ); break;
							case NODE_BAND:		code_write( e, OPERATOR_BAND ); break;
							case NODE_BXOR:		code_write( e, OPERATOR_BXOR ); break;
							case NODE_EQ:		code_write( e, OPERATOR_EQ ); break;
							case NODE_NEQ:		code_write( e, OPERATOR_NEQ ); break;
							case NODE_GT:		code_write( e, OPERATOR_GT ); break;
							case NODE_GTOE:		code_write( e, OPERATOR_GTOE ); break;
							case NODE_LT:		code_write( e, OPERATOR_LT ); break;
							case NODE_LTOE:		code_write( e, OPERATOR_LTOE ); break;
							case NODE_ADD:		code_write( e, OPERATOR_ADD ); break;
							case NODE_SUB:		code_write( e, OPERATOR_SUB ); break;
							case NODE_MUL:		code_write( e, OPERATOR_MUL ); break;
							case NODE_DIV:		code_write( e, OPERATOR_DIV ); break;
							case NODE_MOD:		code_write( e, OPERATOR_MOD ); break;
							default: assert( false ); break;
						}

						--c->stack_;
						break;
					}

					case NODE_UNARY_MINUS:
					{
						assert( n->left_ != nullptr );
						walk( e, n->left_, c );
						code_write( e, OPERATOR_UNARY_MINUS );
						break;
					}

					case NODE_PRIMITIVE_VALUE:
					{
						switch( n->token_->tag_ )
						{
							case TOKEN_INTEGER:	code_write( e, OPERATOR_PUSH_INT ); code_write( e, atoi( n->token_->content_ ) ); break;
							case TOKEN_REAL:	code_write( e, OPERATOR_PUSH_DOUBLE ); code_write_block( e, atof( n->token_->content_ ) ); break;
							case TOKEN_STRING:	code_write( e, OPERATOR_PUSH_STRING ); code_write( e, n->token_->content_ ); break;
							default: assert( false ); break;
						}
						++c->stack_;
						break;
					}
					case NODE_IDENTIFIER_EXPR:
					{
						assert( n->token_->tag_ == TOKEN_IDENTIFIER );
						const auto ident = n->token_->content_;

						const auto top = c->stack_;
						if ( n->left_ != nullptr )
						{
							walk( e, n->left_, c );
						}
						const auto arg_num = c->stack_ -top;

						const auto function = query_function( ident );
						if ( function >= 0 )
						{
							code_write( e, OPERATOR_FUNCTION );
							code_write( e, function );
							code_write( e, arg_num );
						}
						else
						{
							// システム変数
							const auto sysvar = query_sysvar( ident );
							if ( sysvar >= 0 )
							{
								if ( arg_num > 0 )
								{
									raise_error( "システム変数に添え字はありません : %s", ident );
								}

								code_write( e, OPERATOR_PUSH_SYSVAR );
								code_write( e, sysvar );
							}
							else
							{
								// 配列変数
								if ( arg_num > 1 )
								{
									raise_error( "関数がみつかりません、配列変数の添え字は1次元までです@@ %s", ident );
								}

								const auto var = search_variable( e->variable_table_, ident );
								assert( var != nullptr );

								if ( arg_num == 0 )
								{
									code_write( e, OPERATOR_PUSH_INT );
									code_write( e, 0 );
								}

								code_write( e, OPERATOR_PUSH_VARIABLE );
								code_write( e, var );
							}
						}

						c->stack_ = top +1;
						break;
					}

					case NODE_END:
						code_write( e, OPERATOR_END );
						break;

					case NODE_RETURN:
					{
						if ( n->left_ )
						{
							walk( e, n->left_, c );
							--c->stack_;
						}
						code_write( e, OPERATOR_RETURN );
						code_write( e, n->left_==nullptr ? 0 : 1 );
						break;
					}

					case NODE_GOTO:
					{
						const auto label_node = n->left_;
						assert( label_node != nullptr );
						assert( label_node->tag_ == NODE_LABEL );

						const auto label_name = label_node->token_->content_;
						const auto label = search_label( e, label_name );
						if ( label == nullptr )
						{
							raise_error( "goto：ラベルがみつかりません@@ %s", label_name );
						}

						code_write( e, OPERATOR_GOTO );
						code_write( e, label );
						break;
					}
					case NODE_GOSUB:
					{
						const auto label_node = n->left_;
						assert( label_node != nullptr );
						assert( label_node->tag_ == NODE_LABEL );

						const auto label_name = label_node->token_->content_;
						const auto label = search_label( e, label_name );
						if ( label == nullptr )
						{
							raise_error( "gosub：ラベルがみつかりません@@ %s", label_name );
						}

						code_write( e, OPERATOR_GOSUB );
						code_write( e, label );
						break;
					}

					case NODE_REPEAT:
					{
						if ( n->left_ )
						{
							walk( e, n->left_, c );
							--c->stack_;
						}
						else
						{
							code_write( e, OPERATOR_PUSH_INT );
							code_write( e, -1 );
						}
						const auto pos_head = e->execute_code_->code_size_;
						code_write( e, OPERATOR_REPEAT );
						code_write( e, 0 );// dummy TAIL

						if ( c->repeat_depth_ >= sizeof(c->repeat_head_) /sizeof(*c->repeat_head_) )
						{
							raise_error( "repeat-loop: ソースコード上でネストが深すぎます@@ %d行目", n->token_->appear_line_ );
						}
						c->repeat_head_[c->repeat_depth_] = static_cast<int>( pos_head );
						++c->repeat_depth_;

						code_write( e, OPERATOR_REPEAT_CHECK );
						break;
					}
					case NODE_LOOP:
					{
						if ( c->repeat_depth_ <= 0 )
						{
							raise_error( "repeat-loop: repeatがないのにloopを検出しました@@ %d行目", n->token_->appear_line_ );
						}

						const auto loop_head = e->execute_code_->code_size_;
						code_write( e, OPERATOR_LOOP );

						const auto write_offset = c->repeat_head_[c->repeat_depth_ -1] +1;
						e->execute_code_->code_[ write_offset ] = static_cast<int>( loop_head );
						--c->repeat_depth_;
						break;
					}
					case NODE_CONTINUE:		code_write( e, OPERATOR_CONTINUE ); break;
					case NODE_BREAK:		code_write( e, OPERATOR_BREAK ); break;

					case NODE_IF:
					{
						assert( n->left_ != nullptr );
						walk( e, n->left_, c );

						assert( n->right_ != nullptr );
						const auto dispatcher = n->right_;
						assert( dispatcher->tag_ == NODE_IF_DISPATCHER );

						const auto pos_root = e->execute_code_->code_size_;
						code_write( e, OPERATOR_IF );
						code_write( e, 0 );// dummy FALSE

						walk( e, dispatcher->left_, c );
						const auto pos_true_tail = e->execute_code_->code_size_;
						code_write( e, OPERATOR_JUMP_RELATIVE );
						code_write( e, 0 );// dummy TAIL

						const auto pos_false_head = e->execute_code_->code_size_;
						if ( dispatcher->right_ )
						{
							walk( e, dispatcher->right_, c );
						}

						const auto pos_tail = e->execute_code_->code_size_;
						e->execute_code_->code_[ pos_root +1 ] = static_cast<int>( pos_false_head - pos_root );
						e->execute_code_->code_[ pos_true_tail +1 ] = static_cast<int>( pos_tail - pos_true_tail );
						break;
					}
					case NODE_IF_DISPATCHER:
						assert( false );
						break;

					default: assert( false ); break;
				}
			}
		};

		_::walk( e, node, &context );
		st = st->next_;
	}

	if ( context.repeat_depth_ > 0 )
	{
		raise_error( "repeat-loop: 閉じられていないrepeat-loopが存在します" );
	}

	// 何もないならとりあえず書いておく
	if ( e->execute_code_->code_size_ <= 0 )
	{
		code_write( e, OPERATOR_NOP );
	}
}

value_t* evaluate_ast_immediate( ast_node_t* ast )
{
	value_stack_t stack{};
	initialize_value_stack( &stack );

	const bool is_succeeded = evaluate_ast_node( ast, &stack );

	// 戻り値
	value_t* res = nullptr;
	if ( is_succeeded && stack.top_ == 1 )
	{
		res = create_value( *stack_peek( &stack, 0 ) );
		value_isolate( *res );
	}
	uninitialize_value_stack( &stack );
	return res;
}

bool evaluate_ast_node( ast_node_t* n, value_stack_t* stack )
{
	switch( n->tag_ )
	{
		case NODE_EMPTY: break;

		case NODE_EXPRESSION:
			assert( n->left_ != nullptr );
			return evaluate_ast_node( n->left_, stack );

		case NODE_BOR:
		case NODE_BAND:
		case NODE_BXOR:
		case NODE_EQ:
		case NODE_NEQ:
		case NODE_GT:
		case NODE_GTOE:
		case NODE_LT:
		case NODE_LTOE:
		case NODE_ADD:
		case NODE_SUB:
		case NODE_MUL:
		case NODE_DIV:
		case NODE_MOD:
		{
			assert( n->left_ != nullptr );
			if ( !evaluate_ast_node( n->left_, stack ) )
			{
				return false;
			}
			assert( n->right_ != nullptr );
			if ( !evaluate_ast_node( n->right_, stack ) )
			{
				return false;
			}

			if ( stack->top_ < 2 )
			{
				print_error( "式評価：二項演算時に対象の値が存在してません：\n" );
				return false;
			}

			auto* left = stack_peek( stack, -2 );
			auto* right = stack_peek( stack, -1 );
			value_isolate( *left );

			switch( n->tag_ )
			{
				case NODE_BOR:		value_bor( left, *right ); break;
				case NODE_BAND:		value_band( left, *right ); break;
				case NODE_BXOR:		value_bxor( left, *right ); break;
				case NODE_EQ:		value_eq( left, *right ); break;
				case NODE_NEQ:		value_neq( left, *right ); break;
				case NODE_GT:		value_gt( left, *right ); break;
				case NODE_GTOE:		value_gtoe( left, *right ); break;
				case NODE_LT:		value_lt( left, *right ); break;
				case NODE_LTOE:		value_ltoe( left, *right ); break;
				case NODE_ADD:		value_add( left, *right ); break;
				case NODE_SUB:		value_sub( left, *right ); break;
				case NODE_MUL:		value_mul( left, *right ); break;
				case NODE_DIV:		value_div( left, *right ); break;
				case NODE_MOD:		value_mod( left, *right ); break;
				default: assert( false ); break;
			}

			stack_pop( stack, 1 );
			break;
		}

		case NODE_UNARY_MINUS:
		{
			assert( n->left_ != nullptr );
			if ( !evaluate_ast_node( n->left_, stack ) )
			{
				return false;
			}
			if ( stack->top_ < 1 )
			{
				print_error( "式評価：単項演算時に対象の値が存在してません：\n" );
				return false;
			}
			auto* const v = stack_peek( stack, -1 );
			value_isolate( *v );
			value_unary_minus( v );
			break;
		}

		case NODE_PRIMITIVE_VALUE:
		{
			switch( n->token_->tag_ )
			{
				case TOKEN_INTEGER:	stack_push( stack, create_value( atoi( n->token_->content_ ) ) ); break;
				case TOKEN_REAL:	stack_push( stack, create_value( atof( n->token_->content_ ) ) ); break;
				case TOKEN_STRING:	stack_push( stack, create_value( n->token_->content_ ) ); break;
				default: assert( false ); break;
			}
			break;
		}
		default:
			print_error( "式評価：サポートされてないノードの呼び出し（%s）@@ %d行目\n", n->token_->content_, n->token_->appear_line_ + 1 );
			return false;
	}
	return true;
}

//=============================================================================
// ビルトイン

int query_command( const char* s )
{
	static const struct
	{
		int				tag_;
		const char*		word_;
	} table[] =
	{
		{ COMMAND_DEVTERM,		"devterm" },
		{ COMMAND_DIM,			"dim", },
		{ COMMAND_DDIM,			"ddim", },
		{ COMMAND_SDIM,			"sdim", },
		{ COMMAND_POKE,			"poke", },
		{ COMMAND_WPOKE,		"wpoke", },
		{ COMMAND_LPOKE,		"lpoke", },
		{ COMMAND_MES,			"mes", },
		{ COMMAND_INPUT,		"input", },
		{ COMMAND_RANDOMIZE,	"randomize", },
#if NHSP_CONFIG_PERFORMANCE_TIMER
		{ COMMAND_BENCH,		"bench", },
#endif
		{ -1,					nullptr },
	};

	// 全探索
	for( int i=0; table[i].tag_!=-1; ++i )
	{
		if ( string_equal_igcase( s, table[i].word_ ) )
		{ return table[i].tag_; }
	}
	return -1;
}

command_delegate get_command_delegate( builtin_command_tag command )
{
	static const command_delegate commands[] =
	{
		&command_devterm,
		&command_dim,
		&command_ddim,
		&command_sdim,
		&command_poke,
		&command_wpoke,
		&command_lpoke,
		&command_mes,
		&command_input,
		&command_randomize,
		&command_bench,
	};
	static_assert( sizeof( commands ) / sizeof( *commands ) == MAX_COMMAND, "command entry num mismatch" );
	return commands[ command ];
}

int query_function( const char* s )
{
	static const struct
	{
		int				tag_;
		const char*		word_;
	} table[] =
	{
		{ FUNCTION_INT,		"int", },
		{ FUNCTION_DOUBLE,	"double", },
		{ FUNCTION_STR,		"str", },
		{ FUNCTION_PEEK,	"peek", },
		{ FUNCTION_WPEEK,	"wpeek", },
		{ FUNCTION_LPEEK,	"lpeek", },
		{ FUNCTION_RND,		"rnd", },
		{ FUNCTION_ABS,		"abs", },
		{ FUNCTION_ABSF,	"absf", },
		{ FUNCTION_DEG2RAD,	"deg2rad", },
		{ FUNCTION_RAD2DEG,	"rad2deg", },
		{ FUNCTION_SIN,		"sin", },
		{ FUNCTION_COS,		"cos", },
		{ FUNCTION_TAN,		"tan", },
		{ FUNCTION_ATAN,	"atan", },
		{ FUNCTION_EXPF,	"expf", },
		{ FUNCTION_LOGF,	"logf", },
		{ FUNCTION_POWF,	"powf", },
		{ FUNCTION_SQRT,	"sqrt", },
		{ FUNCTION_LIMIT,	"limit", },
		{ FUNCTION_LIMITF,	"limitf", },
		{ FUNCTION_STRLEN,	"strlen", },
		{ -1,				nullptr },
	};

	// 全探索
	for( int i=0; table[i].tag_!=-1; ++i )
	{
		if ( string_equal_igcase( s, table[i].word_ ) )
		{ return table[i].tag_; }
	}
	return -1;
}

function_delegate get_function_delegate( builtin_function_tag function )
{
	static const function_delegate functions[] =
	{
		&function_int,
		&function_double,
		&function_str,
		&function_peek,
		&function_wpeek,
		&function_lpeek,
		&function_rnd,
		&function_abs,
		&function_absf,
		&function_deg2rad,
		&function_rad2deg,
		&function_sin,
		&function_cos,
		&function_tan,
		&function_atan,
		&function_expf,
		&function_logf,
		&function_powf,
		&function_sqrt,
		&function_limit,
		&function_limitf,
		&function_strlen,
	};
	static_assert( sizeof( functions ) / sizeof( *functions ) == MAX_FUNCTION, "function entry num mismatch" );
	return functions[ function ];
}

//=============================================================================
// ユーティリティ
void dump_ast( list_t* ast, bool is_detail )
{
	struct _
	{
		static void dump( int indent, ast_node_t* node, bool is_detail )
		{
			for( int i=0; i<indent; ++i )
			{ printf( "  " ); }
			static const char* nodenames[] =
			{
				"EMPTY",
				"LABEL",
				"BLOCK_STATEMENTS",
				"COMMAND",
				"ARGUMENTS",
				"ASSIGN",
				"ADD_ASSIGN",
				"SUB_ASSIGN",
				"MUL_ASSIGN",
				"DIV_ASSIGN",
				"MOD_ASSIGN",
				"BOR_ASSIGN",
				"BAND_ASSIGN",
				"BXOR_ASSIGN",
				"VARIABLE",
				"EXPRESSION",
				"BOR",
				"BAND",
				"BXOR",
				"EQ",
				"NEQ",
				"GT",
				"GTOE",
				"LT",
				"LTOE",
				"ADD",
				"SUB",
				"MUL",
				"DIV",
				"MOD",
				"UNARY_MINUS",
				"PRIMITIVE_VALUE",
				"IDENTIFIER_EXPR",
				"END",
				"RETURN",
				"GOTO",
				"GOSUB",
				"REPEAT",
				"LOOP",
				"CONTINUE",
				"BREAK",
				"IF",
				"IF_DISPATCHER",
			};
			static_assert( sizeof(nodenames) /sizeof(*nodenames) == MAX_NODE, "nodenames size is not match with MAX_NODE" );
			assert( node->tag_>=0 && node->tag_<MAX_NODE );
			printf( "%s", nodenames[node->tag_] );
			if ( is_detail )
			{ printf( " :%p", node ); }
			if ( node->token_ )
			{ printf( "[%s]", node->token_->content_ ); }
			printf( "\n" );
			if ( node->left_ )
			{ dump( indent +1, node->left_, is_detail ); }
			if ( node->right_ )
			{ dump( indent +1, node->right_, is_detail ); }
		}
	};

	printf( "====ast[%p]====\n", ast );
	list_node_t* st =ast->head_;
	while( st != nullptr )
	{
		ast_node_t* node = reinterpret_cast<ast_node_t*>( st->value_ );
		_::dump( 1, node, is_detail );
		st = st->next_;
	}
	printf( "--------\n" );
}

void dump_variable( list_t* var_table, const char* name, int idx )
{
	printf( "%s[%d]=", name, idx );
	if ( auto v = search_variable( var_table, name ) )
	{
		auto p = variable_data_ptr( *v, idx );
		switch( v->type_ )
		{
			case VALUE_INT:		printf("%d", reinterpret_cast<int*>(p)[0]); break;
			case VALUE_DOUBLE:	printf("%lf", reinterpret_cast<double*>(p)[0]); break;
			case VALUE_STRING:	printf("%s", reinterpret_cast<char*>(p)); break;
			default: assert( false ); break;
		}
	}
	else
	{
		printf( "<nil>" );
	}
	printf( "\n" );
}

void dump_stack( value_stack_t* stack )
{
	printf( "====stack[%p] top[%d] max[%d]\n", stack, stack->top_, stack->max_ );
	for( int i=0; i<stack->top_; ++i )
	{
		const auto v = stack_peek( stack, i );
		switch( v->type_ )
		{
			case VALUE_INT:			printf("%d", v->ivalue_); break;
			case VALUE_DOUBLE:		printf("%lf", v->dvalue_); break;
			case VALUE_STRING:		printf("%s", v->svalue_); break;
			case VALUE_VARIABLE:	printf("var[%s] idx[%d]", v->variable_->name_, v->index_); break;
			default: assert( false ); break;
		}
		printf( "\n" );
	}
	printf( "----\n" );
}

void dump_code( const code_container_t* code )
{
	struct _
	{
		static int dump( int indent, const code_t* codes, int pc )
		{
			for( int i=0; i<indent; ++i )
			{ printf( "  " ); }
			static const char* opnames[] =
			{
				"NOP",

				"PUSH_INT",
				"PUSH_DOUBLE",
				"PUSH_STRING",
				"PUSH_VARIABLE",
				"PUSH_SYSVAR",

				"ASSIGN",
				"ADD_ASSIGN",
				"SUB_ASSIGN",
				"MUL_ASSIGN",
				"DIV_ASSIGN",
				"MOD_ASSIGN",
				"BOR_ASSIGN",
				"BAND_ASSIGN",
				"BXOR_ASSIGN",

				"BOR",
				"BAND",
				"BXOR",

				"EQ",
				"NEQ",
				"GT",
				"GTOE",
				"LT",
				"LTOE",

				"ADD",
				"SUB",
				"MUL",
				"DIV",
				"MOD",

				"UNARY_MINUS",

				"IF",

				"REPEAT",
				"REPEAT_CHECK",
				"LOOP",
				"CONTINUE",
				"BREAK",

				"LABEL",

				"GOSUB",
				"GOTO",

				"COMMAND",
				"FUNCTION",

				"JUMP",
				"JUMP_RELATIVE",
				"RETURN",
				"END",
			};
			static_assert( sizeof(opnames) /sizeof(*opnames) == MAX_OPERATOR, "opnames size is not match with MAX_OPERATOR" );

			const auto op = codes[ pc ];
			assert( op>=0 && op<MAX_OPERATOR );
			printf( "%04d: %s[%d] ", pc, opnames[op], op );

			int offset =0;
			switch( op )
			{
				case OPERATOR_NOP:
					break;

				case OPERATOR_PUSH_INT:
					printf( ": VAL[%d]", codes[ pc +1 ] );
					++offset;
					break;

				case OPERATOR_PUSH_DOUBLE:
				{
					double v =0.0;
					const auto stride = code_get_block( v, codes, pc +1 );
					printf( ": VAL[%lf]", v );
					offset += stride;
					break;
				}

				case OPERATOR_PUSH_STRING:
				{
					const char* v =nullptr;
					const auto stride = code_get_block( v, codes, pc +1 );
					printf( ": VAL[%s]", v );
					offset += stride;
					break;
				}

				case OPERATOR_PUSH_VARIABLE:
				{
					variable_t* var =nullptr;
					const auto stride = code_get_block( var, codes, pc +1 );
					printf( ": VAR[%p=%s]", var, var->name_ );
					offset += stride;
					break;
				}

				case OPERATOR_PUSH_SYSVAR:
				{
					const auto sysvar = codes[ pc +1 ];
					printf( ": VAL[%d]", sysvar );
					++offset;
					break;
				}

				case OPERATOR_ASSIGN:
				case OPERATOR_ADD_ASSIGN:
				case OPERATOR_SUB_ASSIGN:
				case OPERATOR_MUL_ASSIGN:
				case OPERATOR_DIV_ASSIGN:
				case OPERATOR_MOD_ASSIGN:
				case OPERATOR_BOR_ASSIGN:
				case OPERATOR_BAND_ASSIGN:
				case OPERATOR_BXOR_ASSIGN:
					break;

				case OPERATOR_BOR:
				case OPERATOR_BAND:
				case OPERATOR_BXOR:
				case OPERATOR_EQ:
				case OPERATOR_NEQ:
				case OPERATOR_GT:
				case OPERATOR_GTOE:
				case OPERATOR_LT:
				case OPERATOR_LTOE:
				case OPERATOR_ADD:
				case OPERATOR_SUB:
				case OPERATOR_MUL:
				case OPERATOR_DIV:
				case OPERATOR_MOD:
					break;

				case OPERATOR_UNARY_MINUS:
					break;

				case OPERATOR_IF:
				{
					const auto false_head = codes[ pc +1 ];
					printf( ": FALSE[%d]", false_head );
					++offset;
					break;
				}

				case OPERATOR_REPEAT:
				{
					const auto end_position = codes[ pc +1 ];
					printf( ": END[%d]", end_position );
					++offset;
					break;
				}
				case OPERATOR_REPEAT_CHECK:
					break;

				case OPERATOR_LOOP:
				case OPERATOR_CONTINUE:
				case OPERATOR_BREAK:
					break;

				case OPERATOR_LABEL:
					break;

				case OPERATOR_GOSUB:
				case OPERATOR_GOTO:
				{
					label_node_t* label =nullptr;
					const auto stride = code_get_block( label, codes, pc +1 );
					assert( label != nullptr );
					printf( ": LABEL[%p=%s] POS[%d]", label, label->name_, label->position_ );
					offset += stride;
					break;
				}

				case OPERATOR_COMMAND:
				{
					const auto command = codes[ pc +1 ];
					assert( command >= 0 );
					const auto arg_num = codes[ pc +2 ];
					printf( ": COMMAND[%d] ARG[%d]", command, arg_num );
					offset += 2;
					break;
				}
				case OPERATOR_FUNCTION:
				{
					const auto function = codes[ pc +1 ];
					assert( function >= 0 );
					const auto arg_num = codes[ pc +2 ];
					printf( ": FUNCTION[%d] ARG[%d]", function, arg_num );
					offset += 2;
					break;
				}

				case OPERATOR_JUMP:
					printf( ": POS[%d]", codes[ pc +1 ] );
					++offset;
					break;
				case OPERATOR_JUMP_RELATIVE:
					printf( ": OFFSET[%d]", codes[ pc +1 ] );
					++offset;
					break;
				case OPERATOR_RETURN:
				{
					printf( ": ARG[%d]", codes[ pc +1 ] );
					++offset;
					break;
				}

				case OPERATOR_END:
					break;
			}
			printf( "\n" );
			return offset;
		}
	};

	printf( "====code[%p] %d[words]====\n", code, static_cast<int>( code->code_size_ ) );
	for( int i=0; i<static_cast<int>(code->code_size_); ++i )
	{
		i += _::dump( 1, code->code_, i );
	}
	printf( "  %04d: EOC\n", static_cast<int>( code->code_size_ ) );
	printf( "--------\n" );
}


}// namespace neteruhsp

