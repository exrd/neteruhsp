
#include "neteruhsp.hh"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>

int main( int argc, const char* argv[] )
{
	using namespace neteruhsp;

	// オプション
	bool has_error = false;
	const char* filename = nullptr;
	bool show_script = false;
	bool show_preprocessed_script = false;
	bool show_ast = false;
	bool show_execute_code = false;
	bool show_help = false;

	// オプション解析
	for( int i=1/* 0飛ばし */; i<argc; ++i )
	{
		const auto arg = argv[i];
		if ( arg[0] == '-' )
		{
			switch( arg[1] )
			{
				case 'f':
					if ( i+1 < argc )
					{
						++i;
						filename = argv[i];
					}
					else
					{
						fprintf( stderr, "ERROR : cannot read script file path\n" );
						has_error = true;
					}
					break;
				case 's':
					show_script = true;
					break;
				case 'p':
					show_preprocessed_script = true;
					break;
				case 'a':
					show_ast = true;
					break;
				case 'e':
					show_execute_code = true;
					break;
				case 'h':
					show_help = true;
					break;
				default:
					fprintf( stderr, "ERROR : unknown argument :%s\n", arg );
					has_error = true;
					break;
			}
		}
		else
		{
			fprintf( stderr, "ERROR : cannot parse argument :%s\n", arg );
			has_error = true;
			break;
		}
	}

	if ( filename == nullptr )
	{
		fprintf( stderr, "ERROR : have to specify script file\n" );
		has_error = true;
	}

	if ( show_help || has_error )
	{
		printf(
			"neteruhsp : commandline tool options\n"
			"  <bin> [<options>...] -f <SCRIPT_FILE>\n"
			"    -f : specify file path to execute\n"
			"\n"
			"  options are followings\n"
			"    -s : show loaded script file contents\n"
			"    -p : show preprocessed script contents\n"
			"    -a : show abstract-syntax-tree constructed from loaded script\n"
			"    -e : show instruction code for execution\n"
			"    -h : show (this) help\n"
		);
		fflush( stdout );
		fflush( stderr );
		return ( has_error ? -1 : 0 );
	}

	// システムここから
	initialize_system();

	// ファイル読み込み
	size_t script_size = 0;
	char* script = nullptr;
	{
		FILE* file = fopen( filename, "r" );
		if ( file == nullptr )
		{
			printf( "ERROR : cannot read such file %s\n", filename );
			return -1;
		}

		fseek( file, 0, SEEK_END );
		const size_t initial_size = ftell( file );

		size_t buffer_size = initial_size +4;// 初期バッファ
		script = reinterpret_cast<char*>( xmalloc( buffer_size +1 ) );

		fseek( file, 0, SEEK_SET );

		for( ; ; )
		{
			const auto c = fgetc(file);
			if ( c == EOF )
			{ break; }

			const auto ch = static_cast<char>( c );
			if ( buffer_size <= script_size )
			{
				buffer_size *= 2;
				script = reinterpret_cast<char*>( xrealloc( script, buffer_size ) );
			}
			script[script_size++] = ch;
		}
		script[script_size] = '\0';

		fclose( file );
	}
	assert( script != nullptr );

	if ( show_script )
	{
		printf( "====LOADED SCRIPT FILE(%d bytes)\n----begin----\n%s\n----end----\n", static_cast<int>( script_size ), script );
	}

	// 実行
	{
		{
			auto env =create_execute_environment();

			load_arg_t la;
			la.dump_preprocessed_ = show_preprocessed_script;
			la.dump_ast_ = show_ast;
			load_script( env, script, &la );

			if ( show_execute_code )
			{
				printf( "====Instruction Code for execution\n" );
				dump_code( env->execute_code_ );
			}

			execute( env );
			destroy_execute_environment( env );
		}

		printf(
			"====\n"
			">>Execution finished, press ENTER key to exit\n"
		);
		fflush( stdout );
		getchar();
	}

	xfree( script );
	uninitialize_system();

	return 0;
}


