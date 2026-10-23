/* Noter: questions to write about, and templates for an entry. */
#include "noter.h"

const char *const prompts[] = {
    /* the day */
    "What was the best moment of today?",
    "What surprised you today?",
    "What did you learn today, big or small?",
    "Who did you spend time with today, and how was it?",
    "What made you smile today?",
    "What drained your energy today, and what gave it back?",
    "Describe today in three words, then explain them.",
    "What would you do differently if today started again?",
    "What did you do today that your future self will thank you for?",
    "What was hard today, and how did you get through it?",
    /* gratitude */
    "Name three things you are grateful for right now.",
    "Who helped you recently? What would you like to tell them?",
    "What small comfort do you often forget to appreciate?",
    "Which place makes you feel at home, and why?",
    "What is something you have now that you once wished for?",
    /* self */
    "What are you looking forward to this week?",
    "What is on your mind that you have not said out loud?",
    "When did you last feel proud of yourself?",
    "What habit would you like to start, and what is the first step?",
    "What habit would you like to stop, and what does it give you?",
    "What does a good day look like for you?",
    "What are you worried about? Write it down, then write what you can do about it.",
    "Which of your qualities do people notice first? Which do you wish they noticed?",
    "What advice would you give yourself from a year ago?",
    "What are you avoiding, and why?",
    "When do you feel most like yourself?",
    "What are three things you are good at?",
    "What would you try if you knew you could not fail?",
    /* memories */
    "Write about a teacher who changed something for you.",
    "Describe a smell that takes you back in time.",
    "What is your earliest memory?",
    "Write about a trip you will never forget.",
    "Describe your childhood home, room by room.",
    "Which song is tied to a memory, and what is the memory?",
    "Write about a friend you have lost touch with.",
    "What was the best gift you ever got?",
    "Write about a time you were brave.",
    "What is a family tradition you love?",
    /* goals */
    "Where do you want to be in one year? What has to happen first?",
    "What is one goal for this month, and how will you know you reached it?",
    "What did you finish recently? How did it feel?",
    "What is taking too much of your time?",
    "Which skill do you want to learn next, and why?",
    "What does success mean to you today? Has it changed?",
    "List what you want to do before the end of the year.",
    /* the world */
    "What did you read, watch or hear lately that stayed with you?",
    "Describe the view from where you are sitting, in detail.",
    "What is something kind a stranger did?",
    "If you could change one thing in your town, what would it be?",
    "What question would you like to ask someone wiser than you?",
    "Write a letter to someone you love, to read in ten years.",
    "Write a letter to yourself, to open on your next birthday.",
    "What is a belief you held strongly that has changed?",
    "Describe a perfect weekend.",
    "What do you want to remember about this time of your life?",
    "Write about the weather today and how it made you feel.",
    "What are you curious about lately?",
    "What did your body need today? Did it get it?",
    "What is a decision you are glad you made?",
    "Write down a conversation from today as well as you remember it.",
};
const int nprompts = sizeof prompts / sizeof prompts[0];

const char *prompt_of_day(long day) {
    long i = (day * 7 + 3) % nprompts;
    return prompts[i < 0 ? i + nprompts : i];
}

const struct tmpl templates[] = {
    { "Free writing", "" },
    { "Daily reflection",
      "Today in one line:\n\nWhat went well:\n\nWhat was hard:\n\nWhat I learned:\n\nTomorrow I will:\n" },
    { "Gratitude", "Three things I am grateful for today:\n1. \n2. \n3. \n\nWhy the first one matters to me:\n" },
    { "Morning pages", "Morning. How I slept, how I feel, what is on my mind:\n\n" },
    { "Evening review", "The best part of today:\n\nWhat I finished:\n\nWhat I let go of:\n\nOne thing for tomorrow:\n" },
    { "Goals for the week", "This week I want to:\n- \n- \n- \n\nWhat could get in the way:\n\nHow I will handle it:\n" },
    { "Travel day", "Where I am:\nWith whom:\nWhat I saw:\nWhat I ate:\nWhat I want to remember:\n" },
    { "Dream", "Last night I dreamed that\n\nHow it felt:\n\nWhat it might be about:\n" },
    { "Letter", "Dear ,\n\n\n\nWith love,\n" },
    { "Mood check", "Right now I feel:\nBecause:\nWhat would help:\n" },
};
const int ntemplates = sizeof templates / sizeof templates[0];
