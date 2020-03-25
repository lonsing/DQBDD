/*
 * This file is part of DQBDD.
 *
 * Copyright 2020 Juraj Síč
 *
 * DQBDD is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.

 * DQBDD is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.

 * You should have received a copy of the GNU Lesser General Public License
 * along with DQBDD. If not, see <http://www.gnu.org/licenses/>.
 */

#include <iostream>
#include <algorithm>

#include "DQBDDexceptions.hpp"
#include "DQBDDformula.hpp"

//Formula::Formula(const Cudd &mgr) : mgr(mgr) {}

Formula::Formula(const Cudd &mgr, QuantifiedVariablesManager &qvmgr) : QuantifiedVariablesManipulator(qvmgr), mgr(mgr), uVarElimHeur(qvMgr->uVarElimHeur) {}

Formula::Formula(const Cudd &mgr, QuantifiedVariablesManipulator &qvManipulator) : QuantifiedVariablesManipulator(qvManipulator), mgr(mgr), uVarElimHeur(qvMgr->uVarElimHeur) {}

BDD Formula::getMatrix() const {
    return matrix;
}

void Formula::setMatrix(const BDD &matrix) {
    if (mgr.getManager() != matrix.manager()) {
        throw DQBDDexception("Not possible to set matrix of formula with BDD from different CUDD manager.");
    }
    this->matrix = matrix;
    needToRecomputeSupportSet = true;
}

// TODO add needToRecomputeSupportSet which is set to true whenever new matrix is set
VariableSet const &Formula::getSupportSet() {
    if (needToRecomputeSupportSet) {
        supportSet.clear();
        for (unsigned int index : matrix.SupportIndices()) {
            supportSet.insert(Variable(index, mgr));
        }
    }
    needToRecomputeSupportSet = false;
    return supportSet;
}

void Formula::eliminateUnivVar(Variable uVarToEliminate) {
    // TODO duplicate only those that are in the bdd????? -> pozri ako som zrobil pushUnivVar ci to fakt treba
    VariableSet eVarsToDuplicate;
    VariableSet dependentVars = getUnivVarDependencies(uVarToEliminate);
    for (const Variable &dependentVar : dependentVars) {
        if (getSupportSet().contains(dependentVar)) {
            eVarsToDuplicate.insert(dependentVar);
            removeDependency(dependentVar, uVarToEliminate);
        }
    }

    removeUnivVar(uVarToEliminate);
    
    // pair used for replacing existential variables that depend on uVarToEliminate with new ones
    std::vector<BDD> varsToBeReplaced;
    std::vector<BDD> varsToReplaceWith;

    //std::cout << "Duplicating vars ";
    for (Variable eVarToDuplicate : eVarsToDuplicate) {
        //std::cout << eVarToDuplicate.getId() << " ";
        Variable newExistVar = eVarToDuplicate.newVarAtSameLevel();
        addExistVar(newExistVar, getExistVarDependencies(eVarToDuplicate));
        varsToBeReplaced.push_back(eVarToDuplicate);
        varsToReplaceWith.push_back(newExistVar);
    }
    //std::cout << std::endl;


    // TODO what is the FUCKING difference between constrain and restrict
    //std::cout << "Creating BDDs" << std::endl;
    // uVarToEliminate=false where we have old existential variables
    BDD f1 = matrix.Restrict(!uVarToEliminate.getBDD());
    //std::cout << "Restriction 1 finished" << std::endl;
    // uVarToEliminate=true where we have new existential variables
    BDD f2 = matrix.Restrict(uVarToEliminate);
    //std::cout << "Restriction 2 finished" << std::endl;
    f2 = f2.SwapVariables(varsToBeReplaced, varsToReplaceWith);
    //std::cout << "Replacing finished" << std::endl;
    // get their conjuction and thus remove univ_id from the formula
    setMatrix(f1 & f2);
    //std::cout << "BDD created" << std::endl;
}

void Formula::eliminateExistVar(Variable existVarToEliminate) {
    setMatrix(matrix.ExistAbstract(existVarToEliminate));
    removeExistVar(existVarToEliminate);
}

void Formula::eliminateExistVars(VariableSet existVarsToEliminate) {
    if (existVarsToEliminate.empty())
        return;
    
    //std::cout << "Eliminating exist variables ";
    BDD CubeToRemove = mgr.bddOne();
    for (const Variable &eVarToEliminate : existVarsToEliminate) {
        CubeToRemove = CubeToRemove & eVarToEliminate;
        removeExistVar(eVarToEliminate);
        //std::cout << eVarToEliminate.getId() << " ";
    }
    //std::cout << std::endl;
    setMatrix(matrix.ExistAbstract(CubeToRemove));
}

// TODO recreate it to getter of possible exist vars to eliminat

VariableSet Formula::getPossibleExistVarsToEliminate() {
    // TODO if (getUnivVars().size() == qvMgr->getNumberOfUnivVars() // this is prob unimportant && getExistVars().size() == ...)
    // TODO     we can just do some easier stuff here???

    // the set of univ vars on which exist vars that are possible to eliminate need to depend on
    VariableSet univVarsNeededToDependOn;

    // only exist vars in support set need to be eliminated (assume that other will be removed by removeUnusedVars())
    VariableSet existVarsInSupportSet;

    for (const Variable &var : getSupportSet()) {
        if (isVarUniv(var)) {
            // exist vars possible to eliminate have to depend on all univ vars in support set
            univVarsNeededToDependOn.insert(var);
        } else if (isVarExist(var)) {
            existVarsInSupportSet.insert(var);
        }
    }

    VariableSet possibleExistVarsToEliminate;

    for (const Variable &eVar : existVarsInSupportSet) {
        // check if support set does not contain all universal variables
        if (univVarsNeededToDependOn.size() != qvMgr->getNumberOfUnivVars()) {
            // if it does not there can be some univ vars on which some exist var in support set depends on
            for (const Variable &uVar : getExistVarDependencies(eVar)) {
                // if uVar was not in univVarsNeededToDependOn
                if (univVarsNeededToDependOn.insert(uVar).second) {
                    // all exist vars we went trough already do not depend on this uVar and so they are not possible to eliminate
                    possibleExistVarsToEliminate.clear();
                }
            }
        }
        // only those exist vars that are quantified in this subformula and depend on all univ vars we already went can possibly be eliminated
        if (isVarHereQuantified(eVar) && getExistVarDependencies(eVar).size() == univVarsNeededToDependOn.size()) {
            possibleExistVarsToEliminate.insert(eVar);
        }
        
    }

    return possibleExistVarsToEliminate;
}

void Formula::initializeUnivVarEliminationOrder() {
    switch (uVarElimHeur)
    {
    case UnivVarElimHeuristic::NumOfDependenciesOnce:
    {
        univVarsOrderToRemove.assign(getUnivVars().begin(), getUnivVars().end());
        std::sort(univVarsOrderToRemove.begin(), univVarsOrderToRemove.end(),
                    [&](Variable a, Variable b) {
                        return (getUnivVarDependencies(a).size() > getUnivVarDependencies(b).size());
                    }
                );
        break;
    }
    case UnivVarElimHeuristic::NumOfDependenciesContinuous:
        return;
    default:
        throw DQBDDexception("Chosen heuristic to choose next universal variable to eliminate is not implemented.");
        break;
    }
}

Variable Formula::getUnivVarToEliminate() {
    switch (uVarElimHeur)
    {
    case UnivVarElimHeuristic::NumOfDependenciesOnce:
    {
        Variable v = univVarsOrderToRemove.back();
        univVarsOrderToRemove.pop_back();
        while (!getUnivVars().contains(v)) {
            v = univVarsOrderToRemove.back();
            univVarsOrderToRemove.pop_back();
        }
        return v;
    }
    case UnivVarElimHeuristic::NumOfDependenciesContinuous:
    {
        return *std::min_element(getUnivVars().begin(), getUnivVars().end(),
                    [&](Variable a, Variable b) {
                        return (getUnivVarDependencies(a).size() < getUnivVarDependencies(b).size());
                    }
                );
    }
    default:
        throw DQBDDexception("Chosen heuristic to choose next universal variable to eliminate is not implemented.");
        break;
    }
}

/**
 * @brief Iteratively remove universal variables and all possible existential variables in this (sub)formula
 * 
 */
void Formula::eliminatePossibleVars() {
    removeUnusedVars();

    initializeUnivVarEliminationOrder();

    while (!getUnivVars().empty()) {
        //printFormulaStats();
        
        VariableSet existVarsToEliminate = getPossibleExistVarsToEliminate();
        while (existVarsToEliminate.size() !=0) {
            eliminateExistVars(existVarsToEliminate);
            removeUnusedVars();
            if (getUnivVars().empty()) {
                break;
            }
            existVarsToEliminate = getPossibleExistVarsToEliminate();
        }
        
        if (getUnivVars().empty()) {
            break;
        }

        //printFormulaStats();
        //reorder();

        Variable uVarToEliminate = getUnivVarToEliminate();
        std::cout << "Eliminating univ variable " << uVarToEliminate.getId() << std::endl;
        eliminateUnivVar(uVarToEliminate);
        
        removeUnusedVars();
    }

    // If all exist vars are quantified here -> it means we just need to check if BDD != 0
    // because otherwise there is a path to 1 resulting in assignment which satisfies this formula.
    // But we also have to check if there are no universal vars in support set.
    // If size of support set is larger than the number of quantified exist vars
    // then some univ var that is earlier in formula is in support set.
    if (getExistVars().size() == qvMgr->getNumberOfExistVars() // all existential variables are in this formula --> it is not a subformula but whole formula
         && getSupportSet().size() == getExistVars().size()) { // only existential variables are here
        if (!getMatrix().IsZero()) {
            setMatrix(mgr.bddOne());
            clear();
        }
    } else { // this is just a subformula, we cannot just remove existential variables
        // but we can check if there are some leftover exist vars possible to eliminate
        VariableSet existVarsToEliminate = getPossibleExistVarsToEliminate();
        while (existVarsToEliminate.size() !=0) {
            eliminateExistVars(existVarsToEliminate);
            removeUnusedVars();
            existVarsToEliminate = getPossibleExistVarsToEliminate();
        }
    }

    //printFormulaStats();
}

std::ostream& Formula::print(std::ostream& out) const {
    return out << getMatrix();
}

void Formula::printStats() {
    std::cout << "Formula BDD have " << matrix.nodeCount() 
                << " nodes with " << getUnivVars().size() << " universal variables and "
                << getExistVars().size() << " existential variables." << std::endl;
}

/*
int Formula::eliminatePossibleExistVars() {
    VariableSet supportSet = getSupportSet();
    VariableSet univVarsNeededToDependOn;
    VariableSet existVarsToEliminate;

    bool isUnivVarInSupportSet = false;
    bool isFreeVarInSupportSet = false;

    for (const Variable &var : supportSet) {
        if (isVarUniv(var)) {
            if (univVarsNeededToDependOn.insert(var).second) { // if var was not in univVarsNeededToDependOn
                existVarsToEliminate.clear();
            }
            isUnivVarInSupportSet = true;
        } else if (isVarExist(var)) {
            for (const Variable &uVar : getExistVarDependencies(var)) {
                if (univVarsNeededToDependOn.insert(uVar).second) { // if uVar was not in univVarsNeededToDependOn
                    existVarsToEliminate.clear();
                }
            }
            if (isVarHere(var)) {
                if (getExistVarDependencies(var).size() == univVarsNeededToDependOn.size()) {
                    existVarsToEliminate.insert(var);
                }
            } else {
                isFreeVarInSupportSet = true;
            }
        } else {
            isFreeVarInSupportSet = true;
        }
    }

    if (!isUnivVarInSupportSet || !isFreeVarInSupportSet) { // if matrix contains only existential variables that are in this formula
        // we can just look if bbd is Zero, if it is not, then eliminating all exist variables would just leave us with One
        if (!matrix.IsZero()) {
            matrix = mgr.bddOne();
        }
        int numberOfExistVars = getExistVars().size();
        clear();
        return numberOfExistVars;
    } else {
        eliminateExistVars(existVarsToEliminate);
        return existVarsToEliminate.size();
    }    
}
*/

/*
bool Formula::isMatrixOne() {
    return matrix == bddtrue;
}

bool Formula::isMatrixZero() {
    return matrix == bddfalse;
}
*/