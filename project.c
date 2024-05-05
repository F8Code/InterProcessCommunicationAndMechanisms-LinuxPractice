#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <semaphore.h>
#include <sys/wait.h>
#include <sys/msg.h>
#include <fcntl.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>

int shmid1, shmid2;
int *shmseg, *fileFlag, *Pid1, *Pid2, *Pid3, *Pid4;

struct sigaction Pd, PM, P1, P2, P3;

sem_t *SemShm, *Sem1, *Sem2, *Sem3, *SemWait1, *SemWait2;

void PMSignalHandler(int sig, siginfo_t *info, void *uncontext);
void P1SignalHandler(int sig, siginfo_t *info, void *uncontext);
void P2SignalHandler(int sig, siginfo_t *info, void *uncontext);
void P3SignalHandler(int sig, siginfo_t *info, void *uncontext);

int main()
{ 
	//default signal handler
	Pd.sa_handler = SIG_DFL;
	
	//pamiec dzielona dla wartosci sygnalow
	shmid1 = shmget(1111, 2*sizeof(int), IPC_CREAT|0666);
	if(shmid1 == -1) {
		perror("shmget error");
		exit(1);
	}
	shmseg = shmat(shmid1, NULL, 0);
	if(shmseg == (void*) -1) {
		perror("shmat error");
		exit(1);
	}
	fileFlag = shmseg + 4;
	*fileFlag = 0;
	
	//pamiec dzielona dla id pidow
	shmid2 = shmget(2222, 3*sizeof(int), IPC_CREAT|0666);
	if(shmid2 == -1) {
		perror("shmget error");
		exit(1);
	}
	Pid1 = shmat(shmid2, NULL, 0);
	if(Pid1 == (void*) -1) {
		perror("shmat error");
		exit(1);
	}
	Pid2 = Pid1 + 4;
	Pid3 = Pid2 + 4;

	//struktura kolejki komunikatow
	struct word_message {
		long type;
		char word[64];
		int number;
	};
	int msqid = msgget(3333, IPC_CREAT|0666);
	if(msqid == -1) {
		printf("msgget error");
		exit(1);
	}
	//utworzenie struktury kolejki komunikatow
	struct word_message word_msg;
	word_msg.type = 1;
	
	//setup semaforow
	SemShm = sem_open("SemShm", O_CREAT, 0666, 1);
	Sem1 = sem_open("Sem1", O_CREAT, 0666, 0);
	Sem2 = sem_open("Sem2", O_CREAT, 0666, 0);
	Sem3 = sem_open("Sem3", O_CREAT, 0666, 0);
	SemWait1 = sem_open("SemWait1", O_CREAT, 0666, 0);
	SemWait2 = sem_open("SemWait2", O_CREAT, 0666, 0);
	//unlink semaforow zapewnia ich usuniecia w przypadku niepoprawnego wylaczenia programu
	sem_unlink("Sem1");
	sem_unlink("Sem2");
	sem_unlink("Sem3");
	sem_unlink("SemShm");
	sem_unlink("SemWait1");
	sem_unlink("SemWait2");
	
	//word sluzy do przechowywania nazwy pliku, n do przechowywania wyboru uzytkownika
	FILE *file;
	char word[256];
	int n;
	
	pid_t pid1, pid2, pid3;
	pid1 = fork();
	//pid1
	if (pid1 == 0) {	
		printf("Utworzono PID1(%d)\n", getpid());
		//signal handler setup dla pid1
		P1.sa_flags = SA_SIGINFO|SA_RESTART;
		P1.sa_sigaction = &P1SignalHandler;
		sigaction(SIGTSTP, &P1, NULL);
		sigaction(SIGCONT, &P1, NULL);
		sigaction(SIGTERM, &P1, NULL);
		sigaction(SIGUSR1, &P1, NULL);
		sigaction(SIGUSR2, &P1, NULL);
		//oczekiwanie na utworzenie pidow 2 i 3
		sem_wait(SemWait2);
		*Pid1 = getpid();
		while(1) { 
			n = 0;
			printf("Wczytaj dane z pliku(1), z klawiatury(2), zakoncz program(3): ");
			while (n != 1 && n != 2 && n != 3)
				scanf("%d", &n);
			//jesli zakonczenie programu
			if (n == 3) {
				kill(*Pid2, SIGTERM);
				while(1);
			}	
			//jesli czytanie z pliku
			else if (n == 1) {
				*fileFlag = 1;
				//otwieranie pliku o nazwie zadanej przez uzytkownika
				printf("Podaj nazwe pliku: ");
				scanf("%s", word);
				word_msg.word[strcspn(word_msg.word, "\n")] = 0;
				file = fopen(word, "r");
				if (file == NULL)
					printf("Taki plik nie istnieje: \n"); 
				else {
					//czytanie wszystkich slow w pliku, przesyl do pid2
					while(fgets(word_msg.word, sizeof(word), file) != NULL) { 
						word_msg.word[strcspn(word_msg.word, "\n")] = 0;
						//printf("PID1(%d) - Odczytano slowo: %s\n", getpid(), word_msg.word);
						msgsnd(msqid, &word_msg, sizeof(struct word_message) - sizeof(long), 0);
						sem_post(Sem2);
						sem_wait(Sem1);
					}
				}
				*fileFlag = 0;
				if (*shmseg == SIGTERM) kill(*Pid2, SIGTERM);							
			}	
			//jesli czytanie z klawiatury
			else {
				//czytanie slowa z stdin, przesyl do pid2
				printf("PID1(%d) - Podaj slowo: ", getpid());
				scanf("%s", word_msg.word);
				word_msg.word[strcspn(word_msg.word, "\n")] = 0;
				msgsnd(msqid, &word_msg, sizeof(struct word_message) - sizeof(long), 0);	
				sem_post(Sem2);
				sem_wait(Sem1);
			}
		}
	}
	//pid2
	pid2 = fork();
	if (pid2 == 0) {
		printf("Utworzono PID2(%d)\n", getpid());
		//signal handler setup dla pid2
		P2.sa_flags = SA_SIGINFO|SA_RESTART;
		P2.sa_sigaction = &P2SignalHandler;
		sigaction(SIGTSTP, &P2, NULL);
		sigaction(SIGCONT, &P2, NULL);
		sigaction(SIGTERM, &P2, NULL);
		sigaction(SIGUSR1, &P2, NULL);	
		sigaction(SIGUSR2, &P2, NULL);
		sem_wait(SemWait1);
		*Pid2 = getpid();
		//zwolnienie drugiego semafora dopuszczajacego prace pid1
		sem_post(SemWait2); 
		while(1) {
			sem_wait(Sem2);
			//odbior slowa, obliczenie dlugosci, przesyl liczby do pid3
			msgrcv(msqid, &word_msg, sizeof(struct word_message) - sizeof(long), 1, 0);
			word_msg.number = strlen(word_msg.word);
			msgsnd(msqid, &word_msg, sizeof(struct word_message) - sizeof(long), 0);
			sem_post(Sem3);
		}
	}
	//pid3
	pid3 = fork();
	if (pid3 == 0) {
		printf("Utworzono PID3(%d)\n", getpid());
		//signal handler setup dla pid3
		P3.sa_flags = SA_SIGINFO|SA_RESTART;
		P3.sa_sigaction = &P3SignalHandler;
		sigaction(SIGTSTP, &P3, NULL);
		sigaction(SIGCONT, &P3, NULL);
		sigaction(SIGTERM, &P3, NULL);
		sigaction(SIGUSR1, &P3, NULL);
		sigaction(SIGUSR2, &P3, NULL);
		//zwolnienie pierwszego semafora dopuszczajacego prace pid2
		*Pid3 = getpid();
		sem_post(SemWait1);
		while(1) {
			sem_wait(Sem3);
			//odbior liczby, wyswietlenie wyniku w stdin
			msgrcv(msqid, &word_msg, sizeof(struct word_message) - sizeof(long), 1, 0);
			printf("PID3(%d) - Liczba znakow: %d\n", getpid(), word_msg.number);
			sem_post(Sem1);
		}
	}
	//signal handler setup dla PM
	PM.sa_flags = SA_SIGINFO|SA_RESTART;
	PM.sa_sigaction = &PMSignalHandler;
	sigaction(SIGTSTP, &PM, NULL);
	sigaction(SIGCONT, &PM, NULL);
	sigaction(SIGTERM, &PM, NULL);
	
	//czekanie na zakonczenie pracy pidow
	waitpid(pid1, NULL, 0);
	waitpid(pid2, NULL, 0);
	waitpid(pid3, NULL, 0);	
	
	//usuniecie kolejki komunikatow, pamieci wspoldzielonej i semaforow
	msgctl(msqid, IPC_RMID, NULL);
	shmdt(shmseg);
   shmctl(shmid1, IPC_RMID, NULL);
   shmdt(Pid1);
   shmctl(shmid2, IPC_RMID, NULL);
	sem_close(SemShm);
	sem_close(Sem1);
	sem_close(Sem2);
	sem_close(Sem3);
   sem_close(SemWait1);
   sem_close(SemWait2);
	return 0;
}

void PMSignalHandler(int sig, siginfo_t *info, void *uncontext) {
    if (info->si_pid == *Pid2 && sig == SIGTSTP) {
    	  sem_wait(SemShm);
    	  *shmseg = sig;
    	  sem_post(SemShm);
        kill(*Pid1, SIGUSR1);
    }
    else if (info->si_pid == *Pid2 && (sig == SIGCONT || sig == SIGTERM)) {
    	  sem_wait(SemShm);
    	  *shmseg = sig;
    	  sem_post(SemShm);
        kill(*Pid1, SIGUSR2);
    }
}

void P1SignalHandler(int sig, siginfo_t *info, void *uncontext) {
    if (info->si_pid == *Pid1 && sig == SIGTSTP) {
		  pause();
    }
    else if (info->si_pid == getppid() && sig == SIGUSR1) {
    	  sem_wait(SemShm);
        sig = *shmseg;
        sem_post(SemShm);
		  kill(*Pid2, SIGUSR1);
		  kill(*Pid1, sig);
    }
    else if (info->si_pid == getppid() && sig == SIGUSR2) {
    	  sem_wait(SemShm);
        sig = *shmseg;
        sem_post(SemShm);
        kill(*Pid2, SIGUSR2);
		  sigaction(sig, &Pd, NULL);
        kill(getpid(), sig);
        sigaction(sig, &P1, NULL);
    }
}

void P2SignalHandler(int sig, siginfo_t *info, void *uncontext) {
	 if (info->si_pid == *Pid2 && sig == SIGTSTP) {
    	  pause();
    }
    else if (sig == SIGTSTP || sig == SIGCONT || sig == SIGTERM) {
    	  if (sig == SIGTERM && *fileFlag == 1) {
    	  sem_wait(SemShm);
    	  *shmseg = sig;
    	  sem_post(SemShm);
    	  }
		  else kill(getppid(), sig);
    }
    else if (info->si_pid == *Pid1 && sig == SIGUSR1) {
    	  sem_wait(SemShm);
        sig = *shmseg;
        sem_post(SemShm);
		  kill(*Pid3, SIGUSR1); 
		  kill(*Pid2, sig);
    } 
    else if (info->si_pid == *Pid1 && sig == SIGUSR2) {
    	  sem_wait(SemShm);
        sig = *shmseg;
        sem_post(SemShm);
        kill(*Pid3, SIGUSR2);
		  sigaction(sig, &Pd, NULL);
        kill(getpid(), sig);
        sigaction(sig, &P2, NULL);
    }
}

void P3SignalHandler(int sig, siginfo_t *info, void *uncontext) {
	 if (info->si_pid == *Pid3 && sig == SIGTSTP) {
    	  pause();
    }
    else if (info->si_pid == *Pid2 && sig == SIGUSR1) {
        sem_wait(SemShm);
        sig = *shmseg;
        sem_post(SemShm);
        kill(*Pid3, sig); 
    }
    else if (info->si_pid == *Pid2 && sig == SIGUSR2) {
    	  sem_wait(SemShm);
        sig = *shmseg;
        sem_post(SemShm);
		  sigaction(sig, &Pd, NULL);
        kill(getpid(), sig);
        sigaction(sig, &P3, NULL);
    }
}
