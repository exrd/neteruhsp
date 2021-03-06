# neteruhsp is

HSPのコンソール版の、更に機能を絞った版です。

[TinyHSPの提案](http://hsp.tv/play/pforum.php?mode=all&num=77515)をボケーっと見ていて何となく作ったもので、実用的なプロジェクトではありませんので注意してください。

※後継として [nidonehsp](https://github.com/exrd/nidonehsp) があります、興味がある型はそちらも参照してみてください。

## 特徴

以下のような特徴があるかもしれません。

### クロスプラットフォーム

OS依存のライブラリを使っていないため、クロスプラットフォーム対応です。

一方で、OS依存のライブラリを使っていないため、色付き文字の描画などはサポートされません。

### 小さい

仕様を絞っていることもあり、ソースコードはヘッダが800行、実装が6500行、合計で73000行程度と（比較的）コンパクトです。

### VM

抽象構文木を生成した後にバイトコードへ変換し、バイトコードを読みながらの実行を行う、所謂処理系としての最低限の体裁を持っています。

VMとしてはスタックマシンベースです。

## 言語仕様

改めてHSPの言語仕様を書き下すのは難しいので、サンプルコード多めでの説明になります。

それっぽいことが出来ます。

（※改めて書き下してみたら結構量が多かったです、HSPの言語仕様ってそこそこあるのね…。。。）

### 値

整数、倍精度浮動小数点、文字列型がサポートされています。

    a = 12
    b = 12.3
    c = "message"

### 変数

コンパイルを行った段階で16個の要素持つ整数型の配列に初期化されています。

配列は一次元のみサポートしており、配列の要素へのアクセスは`()`を使います。

    a(2) = 12; これはOK、最初から16要素まではある
    mes ""+a(2)
    
    a(0) = a(16); これはNG

変数の型は要素0への代入時に動的に変化しますが、それ以降の要素への代入時は型チェックが入ります。

    a(0) = "string"; これはOK、要素0番目
    mes ""+a(0)
    
    a(2) = 12.5; これはNG
    mes ""+a(2)

変数の型が動的に変化した場合は、それまで変数に入れていた値は（要素0はもちろん、それ以外の要素も）保存されません。

変数名のみ書いた場合は要素0への参照になります。

    a = 12; これはa(0) = 12と同じ
    mes ""+a(0)

明示的に変数の型を指定して初期化したい場合、それぞれの型に応じて初期化コマンド（※後述）を呼びます。

    dim a, 32; 整数型、32要素
    ddim b, 64; 倍精度浮動小数点、64要素
    sdim c, 256, 16; 文字列、1要素256文字まで持てる、16要素

### 演算

大体揃えました。

    mes ""+(10 + 4); 加算
    mes ""+(12.8 - 6.3); 減算
    mes ""+(9 * 4); 乗算
    mes ""+(8 / 2); 除算
    mes ""+(15 \ 7); 剰余
    mes ""+(12 > 5); 比較大なり
    mes ""+(12 >= 5); 比較大なりまたは等しい
    mes ""+(12 < 5); 比較小なり
    mes ""+(12 <= 5); 比較小なりまたは等しい
    mes ""+(12 = 5); 等号
    mes ""+(12 ! 5); 不等号
    mes ""+(12 & 5); ビットAND
    mes ""+(12 | 5); ビットOR
    mes ""+(12 ^ 5); ビットXOR
    mes ""+(-98.2); 単項マイナス

糖衣構文でビットANDとビットORは`and`と`or`でも書けます。

裏を返すとこれらは変数に使えません。

### コマンド

特定の動作をさせるための命令群で、命令を表す識別子を書いた後、カンマ区切りで命令へのパラメータを記述します。

    mes "print message"; コンソール標準出力へ文字列を出力
    input i, 2, 2; 標準入力を受け付け、変数へ取得した文字列を代入。パラメータの意味はコンソール版HSPと一緒で、バッファする文字数とモード
    randomize; 乱数シードを初期化

### 関数

コマンドと似ていますが、何か一つの値を返し式中に使うことができるものです。

    mes ""+rnd(16); 0～パラメータで指定した値までの範囲の乱数を返します
    mes ""+abs(-32); 整数パラメータの絶対値を返します

### コメント

`;`による一行コメントのみ実装しています。

任意の位置で挿入できます。

    ; a = 2; ここはコメントなので実行されない
    mes ""+a

### サブルーチンとラベル

`*<identifier>`と書いた行はラベルとして認識され、他の場所から即座にその行に処理を移すことができます。

ラベルへ処理を移して帰ってこない場合は`goto`、`return`を使って処理を移したところまで戻ってくる場合は`gosub`を使います。

`goto`の場合は次の用な感じ。

    goto *l1
    a = 2; ここは上のgotoによって実行されません
    *l1
    mes ""+a

`gosub`の場合は次のような感じ。

    gosub *l1
    a = 2; ここは上のgosub後に実行されます
    *l1
    mes ""+a
    return; 実はgosub後、「a=2」を実行したあとそのままここまで実行がきてしまうため、ここでエラーになります

`gosub`を使った後`return`で帰る際、`return`に値を渡すことで特殊な変数（システム変数）へ値が代入されます。

代入された値はグローバルに参照可能なので、`gosub`したラベルから値を返す、といった使い方ができます。

    gosub *li
    mes ""+stat; 整数型の場合、statへ代入されます
    gosub *ld
    mes ""+refdval; 浮動小数点型の場合、refdvalへ代入されます
    gosub *ls
    mes ""+refstr; 文字列型の場合、refstrへ代入されます
    end
    
    *li
        return 98
    *ld
        return 97.3
    *ls
        return "str"

### 条件分岐

「ある値が特定の値だったら」や「この値より大きかったら」といった、特定の条件を満たした時だけ実行するためには`if`を使います。

    a = 4
    if a>2 : mes "a>2"; aが2より大きい時だけ実行される

条件が満たされている時と、そうでない時とで異なる処理をしたい場合は`else`を使います。

    mes "please input any string you like:"
    input a, 16, 2
    if ( a == "neko" ) : mes "kawaii" : else : mes "kawaikunai"

コロン`:`で区切ったりブレース`{}`を使うことで複数個の処理を書くこともできます。

    a = 3
    if ( a>2 ) {
        mes "a>2, check next"
        if ( a<4 ) : mes "a>2 and a<4" : else : mes "a>=4"
    } else : mes "a<=2"

### ループ

特定の処理群を特定の回数だけ繰り返したい場合、`repeat`と`loop`が使えます。

    repeat 4
        mes "message"
    loop
    ; 「meesage」と4回表示される

ある場所から次のループへ即座に移ったり、ループ自体を抜け出したい場合は`continue`と`break`を使います。

    sum = 0
    repeat 10
        if ( cnt == 4 ) : continue; 4はカウントしない
        if ( cnt == 8 ) : break; 8以降もカウントしない
        sum = sum +cnt
    loop
    mes ""+sum

何気なく使いましたが、`repeat`～`loop`中で「今何回目のループか」がシステム変数`cnt`に入っています、ループは0始まりです。

### その他の制御構文

`end`でその場で実行を終わらせられます。

    end; ここで実行を終了する
    mes "ended?"; ここは実行されない

GUIでは非同期な処理待受がありますが、CUIではそういうものはないため`stop`はありません。

特定の入力を受け付ける場合は`input`を使ってください。

### その他

特記事項なし。
HSPの下位互換ですね。

## ソースコードについて

[NYSL](http://www.kmonos.net/nysl/)でライセンスしています。

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

### 基本的に

「動けばいい」方針で作ったソースコードです。

そのためコード自体のメンテナンス性、パフォーマンスは霞ほども求めていないものになっているので注意してください。

言語はC++を使っていますが、一部楽に書き下すためにC++の機能を使っているだけで、元々Cで書いてたこともあり全体を通してCライクです。

### ビルド環境

Win10＋VS2015、Ubuntu（14.04）＋clang3.4でビルド確認済み。

（※GCCは明示的なサポートはしてません。）

#### VSの人

`vs`ディレクトリにソリューションがおいてあります。

自分でビルドするとルートの`bin`ディレクトリにバイナリが生成されます。

安定版バイナリはreleaseに置いておくので（多分）、バイナリだけ試したい人はそちらをどうぞ。

#### Linuxの人

ルートに`Makefile`が置いてあります、`make`してください。

ビルドすると`bin/neteruhsp`というバイナリが生成されます。

#### （Macの人）

（確認できる環境がないので分かりません。）

### テストスクリプト

このreadmeに書いてあるサンプルスクリプトはとりあえず通ります。

### トークナイザー

*手書きです。*

パーサーコンテキスト非依存のため難しい処理ではなく、一文字先読みでうまくできるパターンだったので巨大なswitchで処理分岐してます。

入力文字としてはASCIIのみを想定しています、SJISは特定の文字パターンが読めないと思われます。

### パーサー

再帰下降パーサーです、*同じく手書きです。*

パーサー層でメモ化していないので解析処理は非常に低速なオーダーです。

ただ、トークナイザー層でのメモ化だけはしているためか幸い非現実的な処理時間にはなっていないようです。

### 抽象構文木

全てのノードを同じ型で扱っているため、ノード型自体少しメモリ冗長な構造をしています。

一つのノードが持ちうるのは関連するトークン`token_`と、二つの子ノード`left_`と`right_`です。

ブロック内の命令列を持つノードなど複数の子ノードを持つ場合、入れ子にしてタプルの連結として表現しています。
（よくAST表示したときに大変なことになります。）

### 処理系

抽象構文木をトラバースしバイトコードを生成してから、バイトコード上で実行を行います。

ロジックをシンプルにするために、関数の戻り値などは一つとしています。

動作モデル自体はスタックマシンで、バイトコードを読みながらスタックを操作します。
VM化をしてはいますが、基本的なパフォーマンス最適化は行っていません。

## バイナリ

バイナリの使い方、コマンドラインからオプション指定します。

    neteruhsp : commandline tool options
      <exe> [<options>...] -f <SCRIPT_FILE>
        -f : specify file path to execute
    
      options are followings
        -s : show loaded script file contents
        -a : show abstract-syntax-tree constructed from loaded script
        -h : show (this) help

大事なのは`-f <SCRIPT_FILE>`のところで、ここでファイルを指定すると実行してくれます。

内部で生成しているASTを覗きたい、などの欲求がある場合は`-a`を指定すると実行前に標準出力に吐き出してくれます。
        
## 今後の展望など

私個人では「ない！」という感じですが、もしこれをどうにかして何かしたいと思うことがあったとして強いて挙げるなら以下。

- エラー対処
  - 構文解析エラーを分かりやすく
  - 実行時エラーの詳細（元ソースコード位置など）、正常な処理への復帰
- 他の構文対応
  - パーサに処理を足す、実装コスト高め
- 実行可能バイナリ生成
  - ファイルアーカイブ
  - プラグイン組み込み
- 最適化
  - レジスタマシン化
- マルチスレッド化
  - 実行ステータスは別にしてあるので、対応自体は難しくない、ハズ
  - スレッド間の協調動作をどう設計するか
- GUI命令を入れる
  - 入れること自体はあまり難しくない構造、のハズ
  - クロスプラットフォーム対応…

### 勢い余ってやってしまったこと（反省）

当初やる気はなかったんですが、色々あってつい手をだしてしまったシリーズ。

- バイトコード化、 VM
    - 処理系として最低限体裁だけ整えておこう、と思いたった
    - 抽象化フェーズが一段増えたのでそれっぽくなった、気がする
- スタックのvalue_tのみmalloc、freeをキャッシュ化
    - プロファイルしたら案の定最大のボトルネックだった
    - 試しに必要最低限だけ対処してみたらどれくらい早くなるのか気になった
        - 結果、ぱッと見で分かる遅いところは大体消え、VMの内部ループが一番遅いようになった
        - よく見る処理系のボトルネックの一つなので、一応そのぐらいの世代にまで追いついた、とも言える
- プリプロセッサの対応
    - 限定的ながら#if、#endif、#ifdef、#enum、#defineといったプリプロセッサの対応

## このリポジトリについて

上記の通り、特に機能追加含め**更新予定はありません**、参照する場合はご注意ください。

重篤なバグがあった場合は直すかもしれません。

※コミット履歴が消えてますが、普通にgitのコマンドミスでリポジトリごと消し飛ばす自体になってしまいました…。
昔の履歴が見たい方は申し訳ありません、コミット履歴は残っておりません。
