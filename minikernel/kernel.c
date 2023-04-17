/*
 *  kernel/kernel.c
 *
 *  Minikernel. Versin 1.0
 *
 *  Fernando Prez Costoya
 *
 */

/*
 *
 * Fichero que contiene la funcionalidad del sistema operativo
 *
 */

#include "kernel.h"	/* Contiene defs. usadas por este modulo */

/*
 *
 * Funciones relacionadas con la tabla de procesos:
 *	iniciar_tabla_proc buscar_BCP_libre
 *
 */

/*
 * Funcin que inicia la tabla de procesos
 */
static void iniciar_tabla_proc(){
	int i;

	for (i=0; i<MAX_PROC; i++)
		tabla_procs[i].estado=NO_USADA;
}

/*
 * Funcin que busca una entrada libre en la tabla de procesos
 */
static int buscar_BCP_libre(){
	int i;

	for (i=0; i<MAX_PROC; i++)
		if (tabla_procs[i].estado==NO_USADA)
			return i;
	return ERROR_GENERICO;
}

/*
*	Funciones relacionadas con la tabla de mutex:
*	iniciar_tabla_mutex buscar_mutex_libre buscar_mutex_repetidos
* NO_USADA 
*/
static void iniciar_tabla_mutex(){
	for (int i=0; i<NUM_MUT; i++)
		tabla_mutex[i].estado=NO_USADA;
}

static int buscar_mutex_libre_y_no_repetido(char *nombre){
	for (int i=0; i<NUM_MUT; i++){
		if (tabla_mutex[i].estado != NO_USADA && cmp(tabla_mutex[i].nombre, nombre) == 1){
			return ERROR_NOMBRE_REPETIDO;
		}
		if (tabla_mutex[i].estado==NO_USADA)
			return i;
	}
	return ERROR_MAX_NUM_MUTEX;
}

static int buscar_mutex(char *nombre){
	for (int i=0; i<NUM_MUT; i++){
		if (cmp(tabla_mutex[i].nombre, nombre) ==1 ){
			return i;
		}
	}
	return ERROR_MUTEX_NO_EXISTE;
}


/*
 *
 * Funciones que facilitan el manejo de las listas de BCPs
 *	insertar_ultimo eliminar_primero eliminar_elem
 *
 * NOTA: PRIMERO SE DEBE LLAMAR A eliminar Y LUEGO A insertar
 */

/*
 * Inserta un BCP al final de la lista.
 */
static void insertar_ultimo(lista_BCPs *lista, BCP * proc){
	if (lista->primero==NULL)
		lista->primero= proc;
	else
		lista->ultimo->siguiente=proc;
	lista->ultimo= proc;
	proc->siguiente=NULL;
}

/*
 * Elimina el primer BCP de la lista.
 */
static void eliminar_primero(lista_BCPs *lista){

	if (lista->ultimo==lista->primero)
		lista->ultimo=NULL;
	lista->primero=lista->primero->siguiente;
}

/*
 * Elimina un determinado BCP de la lista.
*/
static void eliminar_elem(lista_BCPs *lista, BCP * proc){
	BCP *paux=lista->primero;

	if (paux==proc)
		eliminar_primero(lista);
	else {
		for ( ; ((paux) && (paux->siguiente!=proc));
			paux=paux->siguiente);
		if (paux) {
			if (lista->ultimo==paux->siguiente)
				lista->ultimo=paux;
			paux->siguiente=paux->siguiente->siguiente;
		}
	}
}

/*
 *
 * Funciones relacionadas con la planificacion
 *	espera_int planificador
 */

/*
 * Espera a que se produzca una interrupcion
 */
static void espera_int(){
	int nivel;

	//printk("-> NO HAY LISTOS. ESPERA INT\n");

	/* Baja al mnimo el nivel de interrupcin mientras espera */
	nivel=fijar_nivel_int(NIVEL_1);
	halt();
	fijar_nivel_int(nivel);
}

/*
 * Funcin de planificacion que implementa un algoritmo FIFO.
 */
static BCP * planificador(){
	while (lista_listos.primero==NULL)
		espera_int();		/* No hay nada que hacer */
	return lista_listos.primero;
}

/*
 *
 * Funcion auxiliar que termina proceso actual liberando sus recursos.
 * Usada por llamada terminar_proceso y por rutinas que tratan excepciones
 *
 */
static void liberar_proceso(){
	BCP * p_proc_anterior;

	liberar_imagen(p_proc_actual->info_mem); /* liberar mapa */

	p_proc_actual->estado=TERMINADO;
	eliminar_primero(&lista_listos); /* proc. fuera de listos */

	/* Realizar cambio de contexto */
	p_proc_anterior=p_proc_actual;
	p_proc_actual=planificador();

	printk("-> C.CONTEXTO POR FIN: de %d a %d\n",
			p_proc_anterior->id, p_proc_actual->id);

	liberar_pila(p_proc_anterior->pila);
	cambio_contexto(NULL, &(p_proc_actual->contexto_regs));
        return; /* no debera llegar aqui */
}

/*
 *
 * Funciones relacionadas con el tratamiento de interrupciones
 *	excepciones: exc_arit exc_mem
 *	interrupciones de reloj: int_reloj
 *	interrupciones del terminal: int_terminal
 *	llamadas al sistemas: llam_sis
 *	interrupciones SW: int_sw
 *
 */

/*
 * Tratamiento de excepciones aritmeticas
 */
static void exc_arit(){

	if (!viene_de_modo_usuario())
		panico("excepcion aritmetica cuando estaba dentro del kernel");


	printk("-> EXCEPCION ARITMETICA EN PROC %d\n", p_proc_actual->id);
	liberar_proceso();

        return; /* no debera llegar aqui */
}

/*
 * Tratamiento de excepciones en el acceso a memoria
 * Si se accede a un parametro  de la zona de usuario, no se debe producir panico, sino que se aborta el proceso
 */
static void exc_mem(){

	if (!viene_de_modo_usuario() && zona_mem_proc_usuario == 0)
		panico("excepcion de memoria cuando estaba dentro del kernel");


	printk("-> EXCEPCION DE MEMORIA EN PROC %d\n", p_proc_actual->id);
	liberar_proceso();

        return; /* no debera llegar aqui */
}

/*
 * Tratamiento de interrupciones de terminal
 */
static void int_terminal(){
	char car;

	car = leer_puerto(DIR_TERMINAL);
	printk("-> TRATANDO INT. DE TERMINAL %c\n", car);

        return;
}

/*
 * Tratamiento de interrupciones de reloj
 */
static void int_reloj(){
	num_int_reloj++;
	//printk("-> TRATANDO INT. DE RELOJ Nº %d\n", num_int_reloj);
	tratamiento_uso_procesador();
	tratamiento_int_dormir();
    return;
}

/*
 * Tratamiento de llamadas al sistema
 */
static void tratar_llamsis(){
	int nserv, res;

	nserv=leer_registro(0);
	if (nserv<NSERVICIOS)
		res=(tabla_servicios[nserv].fservicio)();
	else
		res=-1;		/* servicio no existente */
	escribir_registro(0,res);
	return;
}

/*
 * Tratamiento de interrupciuones software
 */
static void int_sw(){

	printk("-> TRATANDO INT. SW\n");

	return;
}

/*
 *
 * Funcion auxiliar que crea un proceso reservando sus recursos.
 * Usada por llamada crear_proceso.
 *
 */
static int crear_tarea(char *prog){
	void * imagen, *pc_inicial;
	int error=0;
	int proc;
	BCP *p_proc;

	proc=buscar_BCP_libre();
	if (proc==-1)
		return -1;	/* no hay entrada libre */

	/* A rellenar el BCP ... */
	p_proc=&(tabla_procs[proc]);

	/* crea la imagen de memoria leyendo ejecutable */
	imagen=crear_imagen(prog, &pc_inicial);
	if (imagen)
	{
		p_proc->info_mem=imagen;
		p_proc->pila=crear_pila(TAM_PILA);
		fijar_contexto_ini(p_proc->info_mem, p_proc->pila, TAM_PILA, pc_inicial, &(p_proc->contexto_regs));
		p_proc->id=proc;
		p_proc->estado=LISTO;
		
		p_proc->dormir = 0;
		p_proc->tiempo_usuario = 0;
		p_proc->tiempo_sistema = 0;

		for(int i = 0; i < NUM_MUT_PROC; i++){
			p_proc->descriptores_mutex[i] = NO_USADA;
		}
		p_proc->num_mutex = 0;

		/* lo inserta al final de cola de listos */
		insertar_ultimo(&lista_listos, p_proc);
		error= 0;
	}
	else
		error= -1; /* fallo al crear imagen */

	return error;
}

/*
 *
 * Rutinas que llevan a cabo las llamadas al sistema
 *	sis_crear_proceso sis_escribir
 *
 */

/*
 * Tratamiento de llamada al sistema crear_proceso. Llama a la
 * funcion auxiliar crear_tarea sis_terminar_proceso
 */
int sis_crear_proceso(){
	char *prog;
	int res;

	printk("-> PROC %d: CREAR PROCESO\n", p_proc_actual->id);
	prog=(char *)leer_registro(1);
	int nivel_interrupcion_previo = fijar_nivel_int(NIVEL_3);
	res=crear_tarea(prog);
	fijar_nivel_int(nivel_interrupcion_previo);
	return res;
}

/*
 * Tratamiento de llamada al sistema escribir. Llama simplemente a la
 * funcion de apoyo escribir_ker
 */
int sis_escribir()
{
	char *texto;
	unsigned int longi;

	texto=(char *)leer_registro(1);
	longi=(unsigned int)leer_registro(2);

	escribir_ker(texto, longi);
	return 0;
}

/*
 * Tratamiento de llamada al sistema terminar_proceso. Llama a la
 * funcion auxiliar liberar_proceso
 */
int sis_terminar_proceso(){

	printk("-> FIN PROCESO %d\n", p_proc_actual->id);

	liberar_proceso();

        return 0; /* no debera llegar aqui */
}
//-----------------------------------------------------------------------------------------

/**
* Funcion que retorna el id del proceso que la invoca
*/
int obtener_id_pr(){
	return p_proc_actual->id;
}

/**
* Llamada que permita que un proceso pueda qudarse bloqueado un plazo de tiempo
* Pautas:
* Modificar el BCP para incluir algún campo relacionado con esta llamada.
* Definir una lista de procesos esperando plazos.
* Incluir la llamada que, entre otras labores, debe poner al proceso en estado bloqueado, reajustar las listas de
BCPs correspondientes y realizar el cambio de contexto.
* Añadir a la rutina de interrupción la detección de si se cumple el plazo de algún proceso dormido. Si es así,
debe cambiarle de estado y reajustar las listas correspondientes.
* Revisar el código del sistema para detectar posibles problemas de sincronización y solucionarlos
adecuadamente.
* Se usan los registros para el paso de parametros del sistema
*/
int dormir(){
	unsigned int segundos = (unsigned int) leer_registro(1);
	//printk("-> PROC %d: DORMIR %d SEGUNDOS\n", p_proc_actual->id, segundos);
	// Fijar nivel de interrupción a 3
	int nivel_interrupcion_previo = fijar_nivel_int(NIVEL_3);

	BCP *p_proc = p_proc_actual;
	p_proc->estado = BLOQUEADO;
	// Los procesos duerment el numero de ticks apropiados
	p_proc->dormir = segundos*TICK;

	// Reajustar listas BCP (primero se elimina y luego se inserta)
	eliminar_primero(&lista_listos);
	insertar_ultimo(&lista_bloqueados, p_proc);


	// Cambio contexto voluntario
	p_proc_actual = planificador();
	cambio_contexto(&(p_proc->contexto_regs), &(p_proc_actual->contexto_regs));
	
	// Restaurar nivel de interrupción
	fijar_nivel_int(nivel_interrupcion_previo);

	return 0;
}

void tratamiento_int_dormir(){
	//printk("-> TRATANDO INT. DE RELOJ PARA PROCESOS DORMIDOS\n");

	BCP *p_proc = lista_bloqueados.primero;
	BCP *siguiente = NULL;

	while(p_proc != NULL){
		siguiente = p_proc->siguiente;
		p_proc->dormir--;
		if(p_proc->dormir == 0){
			int nivel_interrupcion_previo = fijar_nivel_int(NIVEL_3);

			p_proc->estado = LISTO;
			eliminar_elem(&lista_bloqueados, p_proc);
			insertar_ultimo(&lista_listos, p_proc);
			
			fijar_nivel_int(nivel_interrupcion_previo);
		
		}
		p_proc = siguiente;
	}
}

/**
* Llamada que devuelve el numero de interrupciones que se han producio desde que arrancó el sistema. 
* Si recibe un puntero no nulo, devuelve cuantas veces en la interrupcion de reloj se ha detectado que el proceso estaba ejecutado en modo usuario (campo usuario) y cuantas en modo sistema (campo sistema).
* Si no hay proceso listo, estará ejecutando el último que se bloqueó. En este caso, no hay que imputarle tiempo de ejecución.
*/
int tiempos_proceso(){
	//printk("-> PROC %d: TIEMPOS PROCESO\n", p_proc_actual->id);
	struct tiempos_ejec *t_ejec = (struct tiempos_ejec *) leer_registro(1);
	int nivel_interrupcion_previo = fijar_nivel_int(NIVEL_3);
	if(t_ejec != NULL){
		// Flag para saber que estamos en zona de memoria de proceso usuario
		zona_mem_proc_usuario = 1;
		t_ejec->usuario = p_proc_actual->tiempo_usuario;
		t_ejec->sistema = p_proc_actual->tiempo_sistema;
		zona_mem_proc_usuario = 0;
	}
	fijar_nivel_int(nivel_interrupcion_previo);
	return num_int_reloj;
}
void tratamiento_uso_procesador(){
	//printk("-> SUMANDO TICKS AL PROCESO\n");
	BCPptr p_proc = lista_listos.primero;
	if(p_proc != NULL){
		if(viene_de_modo_usuario()){
			p_proc_actual->tiempo_usuario++;
		}else{
			p_proc_actual->tiempo_sistema++;
		}
	}
}

/**
* Crear mutex con el nombre y tipo especificados.
* Devuelve un entero que representa un descriptor para acceder al mutex. En caso de error devuelve un numero negativo.
*/
int crear_mutex(){
	printk("-> PROC %d: CREAR MUTEX %d\n", p_proc_actual->id, p_proc_actual->num_mutex);
	char *nombre = (char *) leer_registro(1);
	int tipo = (int) leer_registro(2);
	Mutex *mutex;
	int descriptor_mutex; /*descriptor del mutex*/

	// Comprobar que no se ha alcanzado el maximo numero de mutex
	while(num_mutex_global >= NUM_MUT){
		printk("ERROR: Se ha alcanzado el maximo numero de mutex, %d \n", num_mutex_global);
		esperar_hueco_mutex();
	}

	// Comprobar que el nombre no sea demasiado largo
	if(len(nombre) > MAX_NOM_MUT){
		printk("ERROR: El nombre del mutex es demasiado largo\n");
		return ERROR_LONGITUD_NOMBRE;
	}

	// Comprobar que proceso no tiene mas de NUM_MUT_PROC
	if(p_proc_actual->num_mutex >= NUM_MUT_PROC){
		printk("ERROR: El proceso %d tiene demasiados mutex, \n", p_proc_actual->id, p_proc_actual->num_mutex);
		return ERROR_MAX_NUM_MUTEX_PROC;
	}

	// Comprobar que no existe un mutex con ese nombre
	descriptor_mutex = buscar_mutex_libre_y_no_repetido(nombre);
	if(descriptor_mutex == ERROR_MAX_NUM_MUTEX){
		printk("ERROR: Se ha alcanzado el maximo numero de mutex\n");
		return ERROR_MAX_NUM_MUTEX;
	}

	if(descriptor_mutex == ERROR_NOMBRE_REPETIDO){
		printk("ERROR: Ya existe un mutex con ese nombre\n");
		return ERROR_NOMBRE_REPETIDO;
	}

	// Crear mutex
	num_mutex_global ++;
	mutex = &tabla_mutex[descriptor_mutex];
	mutex->tipo = tipo;
	mutex->estado = OCUPADO;
	mutex->id_proceso = p_proc_actual->id;
	cpy(mutex->nombre, nombre);

	p_proc_actual->descriptores_mutex[p_proc_actual->num_mutex] = descriptor_mutex;
	p_proc_actual->num_mutex++;

	// Devolver descriptor
	return descriptor_mutex;
}

/**
* 	Funcion auxiliar para esperar hasta que haya hueco en la tabla de mutex
*/
void esperar_hueco_mutex(){
	//printk("-> PROC %d: ESPERANDO HUECO MUTEX\n", p_proc_actual->id);
	p_proc_actual->estado = BLOQUEADO;
	int nivel_interrupcion_previo = fijar_nivel_int(NIVEL_3);
	eliminar_elem(&lista_listos, p_proc_actual);
	insertar_ultimo(&lista_bloqueados, p_proc_actual);
	fijar_nivel_int(nivel_interrupcion_previo);
	BCPptr p_proc = p_proc_actual;
	p_proc_actual = planificador();
	cambio_contexto(&(p_proc->contexto_regs), &(p_proc_actual->contexto_regs));
}

/**
*	Devuelve un descriptor asociado a un mutex ya existente o un número negativo en caso de error
*/
int abrir_mutex(){
	printk("-> PROC %d: ABRIR MUTEX\n", p_proc_actual->id);
	char *nombre = (char *) leer_registro(1);

	int descriptor_mutex = buscar_mutex(nombre);
	if(descriptor_mutex < 0){
		return ERROR_MUTEX_NO_EXISTE;
	}
	// Comprobar que proceso no tiene mas de NUM_MUT_PROC
	if(p_proc_actual->num_mutex >= NUM_MUT_PROC){
		return ERROR_MAX_NUM_MUTEX_PROC;
	}
	p_proc_actual->descriptores_mutex[p_proc_actual->num_mutex] = descriptor_mutex;
	p_proc_actual->num_mutex++;
	return descriptor_mutex;
}

/**
* 	 Cierra el mutex especificado, devolviendo un número negativo en caso de error.
*/
int cerrar_mutex(){
	printk("-> PROC %d: CERRAR MUTEX\n", p_proc_actual->id);
	unsigned int mutexid = (unsigned int) leer_registro(1);
	
	int descriptor = p_proc_actual->descriptores_mutex[mutexid];
	if(descriptor < 0){
		return ERROR_MUTEX_NO_EXISTE;
	}
	Mutex *mutex = &tabla_mutex[descriptor];
	mutex->estado = LIBRE;
	mutex->id_proceso = -1;

	p_proc_actual->descriptores_mutex[mutexid] = -1;
	p_proc_actual->num_mutex--;
	num_mutex_global--;
	return 0;
}

/**
*	Función auxiliar para obtener longitud de un string
*/
int len(char *string){
	int i = 0;
	while(string[i] != '\0'){
		i++;
	}
	return i;
}

/**
*   Funcion auxiliar para comparar si dos string son iguales
*/
int cmp(char *s1, char *s2){
	int i = 0;
	while(s1[i] != '\0' && s2[i] != '\0'){
		if(s1[i] != s2[i]){
			return 0;
		}
		i++;
	}
	if(s1[i] == '\0' && s2[i] == '\0'){
		return 1;
	}
	return 0;
}

/**
*	Funcion auxiliar para copiar un string en otro
*/
void cpy(char *dest, char *orig){
	int i = 0;
	while(orig[i] != '\0'){
		dest[i] = orig[i];
		i++;
	}
	dest[i] = '\0';
}

/*
 *
 * Rutina de inicializacin invocada en arranque
 *
 */
int main(){
	/* se llega con las interrupciones prohibidas */

	instal_man_int(EXC_ARITM, exc_arit); 
	instal_man_int(EXC_MEM, exc_mem); 
	instal_man_int(INT_RELOJ, int_reloj); 
	instal_man_int(INT_TERMINAL, int_terminal); 
	instal_man_int(LLAM_SIS, tratar_llamsis); 
	instal_man_int(INT_SW, int_sw); 

	iniciar_cont_int();			/* inicia cont. interr. */
	iniciar_cont_reloj(TICK);	/* fija frecuencia del reloj */
	iniciar_cont_teclado();		/* inici cont. teclado */

	iniciar_tabla_proc();		/* inicia BCPs de tabla de procesos */

	iniciar_tabla_mutex();		/* inicia mutex de tabla de mutex */

	/* crea proceso inicial */
	if (crear_tarea((void *)"init")<0)
		panico("no encontrado el proceso inicial");
	
	/* activa proceso inicial */
	p_proc_actual=planificador();
	cambio_contexto(NULL, &(p_proc_actual->contexto_regs));
	panico("S.O. reactivado inesperadamente");
	return 0;
}
