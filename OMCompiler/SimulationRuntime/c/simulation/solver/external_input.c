/*
 * This file is part of OpenModelica.
 *
 * Copyright (c) 1998-2014, Open Source Modelica Consortium (OSMC),
 * c/o Linköpings universitet, Department of Computer and Information Science,
 * SE-58183 Linköping, Sweden.
 *
 * All rights reserved.
 *
 * THIS PROGRAM IS PROVIDED UNDER THE TERMS OF THE BSD NEW LICENSE OR THE
 * GPL VERSION 3 LICENSE OR THE OSMC PUBLIC LICENSE (OSMC-PL) VERSION 1.2.
 * ANY USE, REPRODUCTION OR DISTRIBUTION OF THIS PROGRAM CONSTITUTES
 * RECIPIENT'S ACCEPTANCE OF THE OSMC PUBLIC LICENSE OR THE GPL VERSION 3,
 * ACCORDING TO RECIPIENTS CHOICE.
 *
 * The OpenModelica software and the OSMC (Open Source Modelica Consortium)
 * Public License (OSMC-PL) are obtained from OSMC, either from the above
 * address, from the URLs: http://www.openmodelica.org or
 * http://www.ida.liu.se/projects/OpenModelica, and in the OpenModelica
 * distribution. GNU version 3 is obtained from:
 * http://www.gnu.org/copyleft/gpl.html. The New BSD License is obtained from:
 * http://www.opensource.org/licenses/BSD-3-Clause.
 *
 * This program is distributed WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE, EXCEPT AS
 * EXPRESSLY SET FORTH IN THE BY RECIPIENT SELECTED SUBSIDIARY LICENSE
 * CONDITIONS OF OSMC-PL.
 *
 */

#include <string.h>
#include <setjmp.h>

#include "openmodelica.h"
#include "openmodelica_func.h"
#include "simulation_data.h"

#include "util/omc_error.h"
#include "util/omc_file.h"
#include "gc/omc_gc.h"
#include "util/read_csv.h"
#include "util/libcsv.h"
#include "util/read_matlab4.h"

#include "simulation/simulation_runtime.h"
#include "simulation/solver/solver_main.h"
#include "simulation/solver/model_help.h"
#include "simulation/options.h"

static inline void externalInputallocate2(DATA* data, const char *filename);

int externalInputallocate(DATA* data)
{
  int i, j;
  const char * csv_input_file_opt = NULL;
  const char * csv_input_file = NULL;

  csv_input_file_opt = (char*)omc_flagValue[FLAG_INPUT_CSV];
  if(!csv_input_file_opt) {
    data->simulationInfo->external_input.active = 0;
    return 0;
  }

  // If '-inputPath' is specified, prefix the csv input file name with that path.
  if (omc_flag[FLAG_INPUT_PATH]) {
    GC_asprintf(&csv_input_file, "%s/%s", omc_flagValue[FLAG_INPUT_PATH], csv_input_file_opt);
  }
  else {
    csv_input_file = csv_input_file_opt;
  }

  externalInputallocate2(data, csv_input_file);

  if(OMC_ACTIVE_STREAM(OMC_LOG_SIMULATION))
  {
    printf("\nExternal Input");
    printf("\n========================================================");
    for(i = 0; i < data->simulationInfo->external_input.n; ++i){
      printf("\nInput: t=%f   \t", data->simulationInfo->external_input.t[i]);
      for(j = 0; j < data->modelData->nInputVars; ++j){
        printf("u%d(t)= %f \t",j+1,data->simulationInfo->external_input.u[i][j]);
      }
    }
    printf("\n========================================================\n");
  }

  data->simulationInfo->external_input.i = 0;
  return 0;
}



void externalInputallocate2(DATA* data, const char *filename){
  int i, j, k;
  struct csv_data *res = read_csv(filename);
  char ** names;
  int * indx;
  const int nu = data->modelData->nInputVars;
  const int nnu = modelica_integer_min(nu, res ? res->numvars - 1 : 0);

  if (NULL == res) {
    fprintf(stderr, "Failed to read CSV-file %s", filename);
    EXIT(1);
  }

  data->modelData->nInputVars = nu;
  data->simulationInfo->external_input.n = res->numsteps;
  data->simulationInfo->external_input.N = data->simulationInfo->external_input.n;

  data->simulationInfo->external_input.u = (modelica_real**)calloc(data->simulationInfo->external_input.n+1, sizeof(modelica_real*));

  names = (char**)malloc(nu * sizeof(char*));

  for(i = 0; i<data->simulationInfo->external_input.n; ++i){
    data->simulationInfo->external_input.u[i] = (modelica_real*)calloc(nnu, sizeof(modelica_real));
  }

  data->simulationInfo->external_input.t = (modelica_real*)calloc(data->simulationInfo->external_input.n+1, sizeof(modelica_real));

  data->callback->inputNames(data, names);

  indx = (int*)malloc(nu*sizeof(int));
  for(i = 0; i < nu; ++i){
    indx[i] = -1;
    for(j = 0; j < res->numvars; ++j){
      if(strcmp(names[i], res->variables[j]) == 0){
        indx[i] = j;
        break;
      }
    }
  }

  for(i = 0, k= 0; i < data->simulationInfo->external_input.n; ++i)
    data->simulationInfo->external_input.t[i] = res->data[k++];

  for(j = 0; j < nu; ++j){
    if(indx[j] != -1){
      k = (indx[j])*data->simulationInfo->external_input.n;
      for(i = 0; i < data->simulationInfo->external_input.n; ++i){
        data->simulationInfo->external_input.u[i][j] = res->data[k++];
      }
    }
  }

  omc_free_csv_reader(res);
  free(names);
  free(indx);
  data->simulationInfo->external_input.active = data->simulationInfo->external_input.n > 0;
}

int externalInputFree(DATA* data)
{
  if(data->simulationInfo->external_input.active){
    int j;

    free(data->simulationInfo->external_input.t);
    for(j = 0; j < data->simulationInfo->external_input.N; ++j)
      free(data->simulationInfo->external_input.u[j]);
    free(data->simulationInfo->external_input.u);
    data->simulationInfo->external_input.active = 0;
  }
  return 0;
}


int externalInputUpdate(DATA* data)
{
  double u1, u2;
  double t, t1, t2;
  long double dt;
  int i;

  if(!data->simulationInfo->external_input.active){
    return -1;
  }

  t = data->localData[0]->timeValue;
  t1 = data->simulationInfo->external_input.t[data->simulationInfo->external_input.i];
  t2 = data->simulationInfo->external_input.t[data->simulationInfo->external_input.i+1];

  while(data->simulationInfo->external_input.i > 0 && t < t1){
    --data->simulationInfo->external_input.i;
    t1 = data->simulationInfo->external_input.t[data->simulationInfo->external_input.i];
    t2 = data->simulationInfo->external_input.t[data->simulationInfo->external_input.i+1];
  }

  while(t > t2
        && data->simulationInfo->external_input.i+1 < (data->simulationInfo->external_input.n-1)){
    ++data->simulationInfo->external_input.i;
    t1 = data->simulationInfo->external_input.t[data->simulationInfo->external_input.i];
    t2 = data->simulationInfo->external_input.t[data->simulationInfo->external_input.i+1];
  }

  if(t == t1){
    for(i = 0; i < data->modelData->nInputVars; ++i){
      data->simulationInfo->inputVars[i] = data->simulationInfo->external_input.u[data->simulationInfo->external_input.i][i];
    }
    return 1;
  }else if(t == t2){
    for(i = 0; i < data->modelData->nInputVars; ++i){
      data->simulationInfo->inputVars[i] = data->simulationInfo->external_input.u[data->simulationInfo->external_input.i+1][i];
    }
    return 1;
  }

  dt = (data->simulationInfo->external_input.t[data->simulationInfo->external_input.i+1] - data->simulationInfo->external_input.t[data->simulationInfo->external_input.i]);
  for(i = 0; i < data->modelData->nInputVars; ++i){
    u1 = data->simulationInfo->external_input.u[data->simulationInfo->external_input.i][i];
    u2 = data->simulationInfo->external_input.u[data->simulationInfo->external_input.i+1][i];

    if(u1 != u2){
      data->simulationInfo->inputVars[i] =  (u1*(dt+t1-t)+(t-t1)*u2)/dt;
    }else{
      data->simulationInfo->inputVars[i] = u1;
    }
  }
 return 0;
}

