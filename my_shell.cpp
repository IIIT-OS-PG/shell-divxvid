#include <cstdio>
#include <iostream>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <string.h>
#include <string>
#include <vector>
#include <map>
#include <stdlib.h>
#include <termios.h>
#include <fstream>
#include <sstream>

#define CONFIG_FILE ".divrc"
#define OVERRIDE_ENV

using namespace std ;

extern char **environ ;
map<string, string> env_variables, alias_store ;
vector<string> history_of_commands ;
int last_return_status ;

class raw_input {
	struct termios old_config, new_config ;
public:
	raw_input() {
		tcgetattr(STDIN_FILENO,&old_config);
		new_config=old_config;
		new_config.c_lflag &=(~ICANON & ~ECHO);
		tcsetattr(STDIN_FILENO,TCSANOW,&new_config);
	}
	~raw_input() {
		tcsetattr(STDIN_FILENO,TCSANOW,&old_config);	
	}
	string get_line() {
		char buffer[1024] ;
		char c ;
		int i = 0 ;
		int idx = history_of_commands.size() -1 ;
		while((c = getchar())) {
			if(c == 27) {
				/*
				left : 68
				right : 67
				up : 65
				down : 66
				 */
				int x = getchar() ;
				int y = getchar() ;
				if(y == 65 || y == 66) {
					continue ;
				}
			}
			putchar(c) ;
			if(c == 10) {
				buffer[i] = '\0' ;
				break;
			}
			if(c > 31 && c < 127) {
				buffer[i++] = c ;
			}
			if(c == 127 && i > 0) {
				putchar('\b');
				putchar(' ');
				putchar('\b');
				i-- ;
			}
		}
		string temp(buffer) ;
		return temp ;
	}
} ;


vector<string> parse_input(string&, const char*);
void execute_normal_command(string&, int, string&) ;
void execute_piped_command(vector<string>&, int, string&) ;
vector<string> detect_append_redir(string&) ;
string get_command(string) ;
void handle_alias(map<string, string>&, string&) ;
map<string, string> initialize_shell() ;
void cd_impl(string&) ;
int parse_command(string&, vector<string>&, string&, bool) ;
void reset_config_file() ;

int main() {

	env_variables = initialize_shell();	

	string PS1 ;
	last_return_status = 0 ;
	if(env_variables.find("PS1") != env_variables.end()) {
		PS1 = env_variables["PS1"] ;
	} else {
		PS1 = "$ " ;
	}

	const int uid = getuid();
	if(uid == 0) {
		PS1 = "# " ;
	}

	//map<string, string> alias_store ; 
	string input ;
	int redirection_status ;
	raw_input rip ;

	while(1) {

		redirection_status = 0 ;
		string file_name = "" ;
		cout << PS1 ;
		//getline(cin, input) ;
		input = rip.get_line() ;
		if(input == "exit")
			break ;
		if(input=="")
			continue ;
		history_of_commands.push_back(input) ;
		string command_got = get_command(input) ;
		if(command_got == "alias") {
			handle_alias(alias_store, input);
			continue ;
		} else if(command_got == "cd") {
			vector<string> temp = parse_input(input, " ") ;
			cd_impl(temp.back());
			continue ;
		} else if(command_got == "resetconfig") {
			reset_config_file();
			break ;
		} else if(command_got == "history") {
			for(const string &st : history_of_commands)
				cout << st << endl ;
			continue ;
		} else if(command_got.substr(0, 4) == "PS1=" && PS1 != "# ") {
			vector<string> temp = parse_input(input, "=") ;
			PS1 = temp.back();
			continue ;
		} 
		vector<string> parsed_piped_input;
		redirection_status = parse_command(input, parsed_piped_input, file_name, false);

		if(parsed_piped_input.size() == 1) {
			//a command without a pipe.
			execute_normal_command(parsed_piped_input[0], redirection_status, file_name) ;
		} else {
			execute_piped_command(parsed_piped_input, redirection_status, file_name) ;
		}
	}

	return 0 ;
}

int parse_command(string& input, vector<string> &parsed_piped_input, string& file_name, bool is_alias) {
	int redirection_status = 0 ;
	vector<string> append_redir_parsed = detect_append_redir(input) ;
	if(append_redir_parsed.size() != 1) {
		redirection_status = 2 ;
		vector<string> temp = parse_input(append_redir_parsed.back(), " ") ;
		file_name = temp.back() ;
	}

	vector<string> output_redir_parsed = parse_input(append_redir_parsed[0], ">") ;
	if(output_redir_parsed.size() != 1) {
		redirection_status = 1 ;
		vector<string> temp = parse_input(output_redir_parsed.back(), " ") ;
		file_name = temp.back() ;
	}

	if(is_alias && redirection_status != 0) {
		cout << "Sorry cannot handle redirection in alias right now." << endl ;
		return -1 ;
	}

	vector<string> pipe_op = parse_input(output_redir_parsed[0], "|");
	for(string &x : pipe_op) {
		string cmd ; // = get_command(x) ;
		stringstream ss(x) ;
		ss >> cmd ;
		if(alias_store.find(cmd) != alias_store.end()) {
			vector<string> parsed_alias ;
			string waste ;
			int redir = parse_command(alias_store[cmd], parsed_alias, waste, true) ;
			if(redir != -1) {
				for(string &pa : parsed_alias) {
					parsed_piped_input.push_back(pa);
				}
				stringstream tempx; 
				while((ss >> cmd)) { tempx << cmd ; }
				parsed_piped_input[ parsed_piped_input.size() - 1 ] = parsed_piped_input.back() + tempx.str() ;
			}
		} else if(cmd == "echo") {
			int pos = input.find("echo") + 4 ;
			while(input[pos] == ' ') {pos++;}
			string temps = input.substr(pos, input.length());
			if(temps[0] == '"' || temps[0] == '\'') {
				temps = temps.substr(1, temps.length()-2) ;
			} else if(temps[0]=='$') {
				temps = temps.substr(1, temps.length()) ;
				if(env_variables.find(temps) != env_variables.end()) {
					temps = env_variables[temps] ;
				} else if(temps == "$") {
					int pid = getpid() ;
					temps = to_string(pid) ;
				} else if(temps == "?") {
					temps = to_string(last_return_status) ;
				}
			}
			parsed_piped_input.push_back(cmd+" "+temps);
		} else {
			parsed_piped_input.push_back(x);
		} 
	}
	return redirection_status ;
}

void reset_config_file() {
	fstream file ;
	char *path, *home, *user ;
	path = getenv("PATH");
	home = getenv("HOME");
	user = getenv("USER");

	char host_name[512] ;
	gethostname(host_name, sizeof host_name);
	
	file.open(CONFIG_FILE, fstream::out | fstream::trunc);

	file << "PATH='" << path << "'\n" ;
	file << "HOME='" << home << "'\n" ;
	file << "USER='" << user << "'\n" ;
	file << "HOSTNAME='" << host_name << "'\n" ;
	file << "PS1='$> '\n" ;

	file.close();	
}

map<string, string> initialize_shell() {
	
	fstream file ;
	map<string, string> env_variables ;
	string line ;

	file.open(CONFIG_FILE);
	if(!file.good()) {
		//if the config file is not found then reset the file to it's core defaults.
		reset_config_file();
		file.open(CONFIG_FILE);
	}	

	int i = 0 ;
	char* envr[64] ;
	while(getline(file, line)) {
		//updating the environment variable.
		string S = line ;
		vector<string> temp = parse_input(S, "'");
		string value = temp[1] ;
		string key = parse_input(temp[0], " =")[0] ;
		env_variables[key] = value ;

		if(key == "HOME" || key == "PATH" || key == "USER" || key == "HOSTNAME") {
			value = key+"="+value ;
			envr[i] = (char*)malloc(value.size());
			memcpy(envr[i++], (char*) value.c_str(), value.length()) ;
			//cout << "written : " << envr[i-1] << endl ;
		}
	}
	envr[i] = NULL ;

	#ifdef OVERRIDE_ENV
		memcpy(environ, envr, sizeof envr) ;
	#endif
	
	file.close() ;
	return env_variables ;	
}  

vector<string> parse_input(string& input, const char* delimiter) {
	vector<string> parsed_strings ;
	char *inp = (char*) input.c_str() ;
	char* token = strtok(inp, delimiter);
	while(token != NULL) {
		parsed_strings.emplace_back(token);
		token = strtok(NULL, delimiter);
	}
	return parsed_strings ;
}

void execute_normal_command(string& command, int redir_status, string& file_name) {
	vector<string> parsed_cmd = parse_input(command, " \n");

	char* argv[parsed_cmd.size()+1];
	for(int i = 0 ; i < parsed_cmd.size() ; i++) {
		argv[i] = (char*) parsed_cmd[i].c_str();
	}
	argv[ parsed_cmd.size() ] = NULL ;

	int pid = fork();
	if(pid == 0) {
		int fd = 1 ;
		if(redir_status != 0) {
			char* tmp_f_name = (char*) file_name.c_str();
			fd = open(tmp_f_name, O_WRONLY | O_CREAT | (redir_status == 1 ? O_TRUNC : O_APPEND), 0644);
		}
		dup2(fd, 1);

		last_return_status = execvp(argv[0], argv) ; 
		if(last_return_status < 0) {
			cout << "Cannot Execute Command" << endl ;
		}
		if(redir_status != 0) {
			close(fd) ;
		}
		exit(0);
	} else {
		wait(NULL) ;
	}
}

void execute_piped_command(vector<string>& commands, int redir_status, string& file_name) {
	int pipe_fd[2] ;
	int last_fd ;
	for(int i = 0, lim = commands.size(); i < lim ; i++) {
		pipe(pipe_fd) ;
		int child_pid = fork() ;
		if(child_pid == 0) {
			int fd = 1 ;
			if(i == 0) {
				dup2(pipe_fd[1], 1) ;
				close(pipe_fd[0]) ;
				close(pipe_fd[1]);
			} else if(i == lim-1) {				
				dup2(last_fd, 0) ;
				close(pipe_fd[0]) ;
				close(pipe_fd[1]) ;
				if(redir_status != 0) {
					char* tmp_f_name = (char*) file_name.c_str();
					fd = open(tmp_f_name, O_WRONLY | O_CREAT | (redir_status == 1 ? O_TRUNC : O_APPEND), 0644);
				}
				dup2(fd, 1);
			} else {
				dup2(last_fd, 0) ;
				dup2(pipe_fd[1], 1) ;
				close(pipe_fd[0]);
				close(pipe_fd[1]);
			}

			vector<string> parsed_cmd = parse_input(commands[i], " \n");

			char* argv[parsed_cmd.size()+1];
			for(int i = 0 ; i < parsed_cmd.size() ; i++) {
				argv[i] = (char*) parsed_cmd[i].c_str();
			}
			argv[ parsed_cmd.size() ] = NULL ;

			last_return_status = execvp(argv[0], argv) ; 
			if(last_return_status < 0) {
				cout << "Cannot Execute Command" << endl ;
			}

			if(i == lim-1 && redir_status != 0) {
				close(fd) ;
			}
			exit(0);
		} else {
			wait(NULL) ;
			last_fd = pipe_fd[0] ;
			close(pipe_fd[1]);
		}
	}
}

vector<string> detect_append_redir(string& input) {
	vector<string> temp ;
	auto loc = input.find(">>") ;
	if(loc != string::npos) {
		temp.push_back( input.substr(0, loc) );
		temp.push_back( input.substr(loc+1, input.length()) ) ;
	} else {
		temp.push_back(input) ;
	}
	return temp ;
}

string get_command(string input) {
	char *val = (char*) input.c_str();
	char *cmd = strtok(val, " ");
	string command(cmd) ;
	return command ;
}

void handle_alias(map<string, string>& alias_map, string& command) {
	vector<string> parts = parse_input(command, "=") ;
	string alias = parse_input(parts[0], " ").back() ;
	string aliased_command = parts.back() ;
	int tick_pos1 = aliased_command.find('\'');
	int tick_pos2 = aliased_command.find('\'', tick_pos1 + 2);
	aliased_command = aliased_command.substr(tick_pos1+1, tick_pos2-tick_pos1-1);
	alias_map[alias] = aliased_command ;
}

void cd_impl(string& path) {
	if(path[0] == '~') {
		path = env_variables["HOME"]+path.substr(1, path.length()) ;	
	}
	const char* pth = path.c_str() ;
	chdir(pth);
}
