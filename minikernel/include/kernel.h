/*
 *  minikernel/include/kernel.h
 *
 *  Minikernel. Versi�n 1.0
 *
 *  Fernando P�rez Costoya
 *
 */

/*
 *
 * Fichero de cabecera que contiene definiciones usadas por kernel.c
 *
 *      SE DEBE MODIFICAR PARA INCLUIR NUEVA FUNCIONALIDAD
 *
 */

#ifndef _KERNEL_H
#define _KERNEL_H

#include "const.h"
#include "HAL.h"
#include "llamsis.h"

/*
 *
 * Definicion del tipo que corresponde con el BCP.
 * Se va a modificar al incluir la funcionalidad pedida.
 *
 */
typedef struct BCP_t *BCPptr;

typedef struct BCP_t {
	int id;				/* ident. del proceso */
	int estado;			/* TERMINADO|LISTO|EJECUCION|BLOQUEADO*/
	contexto_t contexto_regs;	/* copia de regs. de UCP */
	void * pila;		/* dir. inicial de la pila */
	BCPptr siguiente;	/* puntero a otro BCP */
	void *info_mem;		/* descriptor del mapa de memoria */
	int dormir;			/* tiempo que debe dormir */	
	int tiempo_sistema;		/* tiempo de ejecucion en modo sistema */
	int tiempo_usuario;		/* tiempo de ejecucion en modo usuario */
	int descriptores_mutex[NUM_MUT_PROC];			/* descriptor mutex que tiene asociado el proceso */
	int num_mutex; /* numero de mutex que tiene el proceso */
	int vida; /* TICKS que le quedan al proceso */
} BCP;

/*
 *
 * Definicion del tipo que corresponde con la cabecera de una lista
 * de BCPs. Este tipo se puede usar para diversas listas (procesos listos,
 * procesos bloqueados en sem�foro, etc.).
 *
 */

typedef struct{
	BCP *primero;
	BCP *ultimo;
} lista_BCPs;


/*
 * Variable global que identifica el proceso actual
 */

BCP * p_proc_actual=NULL;

/*
 * Variable global que representa la tabla de procesos
 */

BCP tabla_procs[MAX_PROC];

/*
 * Variable global que representa la cola de procesos listos
 */
lista_BCPs lista_listos= {NULL, NULL};

/*
 * Variable global que representa la cola de procesos bloqueados que estan esperando plazos (llamada dormir(int segundos); )
 */
lista_BCPs lista_bloq_dormir= {NULL, NULL};

/*
*	Variable global que representa el numero total de interrupciones de reloj
*/
int num_int_reloj = 0;
/*
*	Variable global que representa si estamos accediendo a una zona de memoria del proceso de usuario
* 0: representa que no estamos accediendo a una zona de memoria del proceso de usuario
*/
int zona_mem_proc_usuario = 0;

/*
 *
 * Definici�n del tipo que corresponde con una entrada en la tabla de
 * llamadas al sistema.
 *
 */
typedef struct{
	int (*fservicio)();
} servicio;

// Estructura para guardar los tiempos de ejecucion de los procesos
struct tiempos_ejec {
    int usuario;
    int sistema;
};

/**
*	Configuracion del mutex
*/
int num_mutex_global = 0; /* Numero de mutex creados*/

// Estados mutex
#define NO_USADO -1
#define LIBRE 0
#define OCUPADO 1

// Tipo de mutex
#define NO_RECURSIVO 0 
#define RECURSIVO 1

// Tipo de errores
#define ERROR_GENERICO -1
#define ERROR_LONGITUD_NOMBRE -10
#define ERROR_MAX_NUM_MUTEX -11
#define ERROR_NOMBRE_REPETIDO -12
#define ERROR_MAX_NUM_MUTEX_PROC -13
#define ERROR_MUTEX_NO_EXISTE -14

// Estructura para guardar los mutex
typedef struct mutex {
	char nombre[MAX_NOM_MUT];
	int tipo;
	int estado;
	int n_proc_asociados; /*numero de veces que se ha abierto*/
	int procs_asociados[MAX_PROC]; /*procesos que han creado|abierto este mutex*/
	// Para lock
	int id_proceso_lock; /*id del proceso que tiene el mutex*/
	int n_veces_lock; /*n veces que se ha hecho lock de este mutex*/
	lista_BCPs procesos_bloqueados; /*lista de procesos bloqueados*/
} Mutex;

Mutex tabla_mutex[NUM_MUT];

lista_BCPs lista_bloq_mutex= {NULL, NULL};

// Round Robin
int id_proc_a_expulsar = NO_USADO;
void tratamiento_round_robin();

// Funciones auxiliares
void esperar_hueco_mutex(char *nombre);
int cumple_requisitos(char *nombre, int descriptor_mutex);
int esta_mutex_asociado_a_proc(int des);
int len(char *string);
int cmp(char *s1, char *s2);
void cpy(char *dest, char *orig);

/*
 * Prototipos de las rutinas que realizan cada llamada al sistema
 */
int sis_crear_proceso();
int sis_terminar_proceso();
int sis_escribir();
int obtener_id_pr();
int dormir();
int tiempos_proceso();
int crear_mutex();
int abrir_mutex();
int cerrar_mutex();
int lock();
int unlock();


/*
 * Variable global que contiene las rutinas que realizan cada llamada
 */
servicio tabla_servicios[NSERVICIOS]={	
	{sis_crear_proceso},
	{sis_terminar_proceso},
	{sis_escribir},
	{obtener_id_pr},
	{dormir},
	{tiempos_proceso},
	{crear_mutex},
	{abrir_mutex},
	{cerrar_mutex},
	{lock},
	{unlock},
};

/**
*	Tratamiento de la interrupcion de reloj para la llamada dormir(int segundos);
*/
void tratamiento_int_dormir();
void tratamiento_uso_procesador();

#endif /* _KERNEL_H */

