#pragma once

enum class Subtask
{
    TakeAndPut,
    Operate,
    Goto
};

struct Task
{
    int id = 0;
    int count = 1;
    Subtask subtask = Subtask::TakeAndPut;
    int a = 0;
    int b = 0;
};