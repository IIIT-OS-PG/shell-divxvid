#include <cstdio>
#include <iostream>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <string.h>
#include <string>
#include <vector>
#include <map>

using namespace std ;

vector<string> parse_input(string&, const char*);
void execute_normal_command(string&, int, string&) ;
void execute_piped_command(vector<string>&, int, string&) ;
vector<string> detect_append_redir(string&) ;
string get_command(string) ;
void handle_alias(map<string, string>&, string&) ;

int main() {

	const string PS1 = "$> " ;
	map<string, string> alias_store ; 
	string input ;
	int redirection_status ;

	while(1) {
		redirection_status = 0 ;
		string file_name = "" ;
		cout << PS1 ;
		getline(cin, input) ;
		if(input == "exit")
			break ;
		if(input=="")
			continue ;
		string command_got = get_command(input) ;
		if(command_got == "alias") {
			handle_alias(alias_store, input);
			continue ;
		} 
		if( alias_store.find(command_got) != alias_store.end() ) {
			//this command is present in the alias store
			input = alias_store[command_got] ;
		}

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

		vector<string> parsed_piped_input = parse_input(output_redir_parsed[0], "|");
		if(parsed_piped_input.size() == 1) {
			//a command without a pipe.
			execute_normal_command(parsed_piped_input[0], redirection_status, file_name) ;
		} else {
			execute_piped_command(parsed_piped_input, redirection_status, file_name) ;
		}
	}

	return 0 ;
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

		if(execvp(argv[0], argv) < 0) {
			cout << "Cannot Execute Command" << endl ;
		}
		if(redir_status != 0) {
			close(fd) ;
		}
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

			if(execvp(argv[0], argv) < 0) {
				cout << "Could not execute command : " << argv[0] << endl ;
			}

			if(i == lim-1 && redir_status != 0) {
				close(fd) ;
			}
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